#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "dhcp.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/platform.h"
#include "pico/stdlib.h"
#include "pico/stdio_uart.h"

#include "wizchip_conf.h"

// Ensure FreeRTOS interrupt handlers are correctly aliased for ARM Cortex-M0

#define WIZ_PIN_CS 20
#define WIZ_PIN_SCK 21
#define WIZ_PIN_MISO 22
#define WIZ_PIN_MOSI 23
#define WIZ_PIN_INT 24
#define WIZ_PIN_RST 25

#define IP_MODE_SWITCH_PIN 14
#define IP_MODE_SWITCH_ENABLE 0
#define IP_MODE_SWITCH_ACTIVE_LOW 1
#define IP_MODE_DEFAULT_DHCP false

#define DHCP_SOCKET 0

static uint8_t dhcp_buffer[548];
static bool wiz_reset_active_low = true;
static bool wiz_spi_cpol = false;
static bool wiz_spi_cpha = false;

static wiz_NetInfo net_info = {
    .mac = {0x00, 0x08, 0xDC, 0x11, 0x22, 0x33},
    .ip = {192, 168, 1, 150},
    .sn = {255, 255, 255, 0},
    .gw = {192, 168, 1, 1},
    .dns = {8, 8, 8, 8},
    .dhcp = NETINFO_STATIC
};

void vAssertCalled(const char *file, int line) {
    printf("ASSERT: %s:%d\r\n", file, line);
    taskDISABLE_INTERRUPTS();
    while (true) {
    }
}

void vApplicationMallocFailedHook(void) {
    puts("FreeRTOS malloc failed");
    taskDISABLE_INTERRUPTS();
    while (true) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name) {
    (void)task;
    printf("Stack overflow: %s\r\n", task_name ? task_name : "unknown");
    taskDISABLE_INTERRUPTS();
    while (true) {
    }
}

static void wiz_cs_select(void) {
    gpio_put(WIZ_PIN_CS, 0);
}

static void wiz_cs_deselect(void) {
    gpio_put(WIZ_PIN_CS, 1);
}

static inline void wiz_spi_set_sck(bool level) {
    gpio_put(WIZ_PIN_SCK, level ? 1 : 0);
}

static inline uint8_t wiz_spi_transfer_byte(uint8_t tx) {
    uint8_t rx = 0;

    const bool idle = wiz_spi_cpol;
    const bool edge1 = !idle;
    const bool edge2 = idle;

    for (int bit = 7; bit >= 0; --bit) {
        uint8_t bitval = (tx >> bit) & 0x01;
        if (!wiz_spi_cpha) {
            gpio_put(WIZ_PIN_MOSI, bitval);
            wiz_spi_set_sck(edge1);
            busy_wait_us_32(1);
            rx = (uint8_t)((rx << 1) | (gpio_get(WIZ_PIN_MISO) ? 1 : 0));
            busy_wait_us_32(1);
            wiz_spi_set_sck(edge2);
        } else {
            wiz_spi_set_sck(edge1);
            busy_wait_us_32(1);
            gpio_put(WIZ_PIN_MOSI, bitval);
            wiz_spi_set_sck(edge2);
            busy_wait_us_32(1);
            rx = (uint8_t)((rx << 1) | (gpio_get(WIZ_PIN_MISO) ? 1 : 0));
            busy_wait_us_32(1);
        }
    }

    wiz_spi_set_sck(idle);
    return rx;
}

static uint8_t wiz_spi_readbyte(void) {
    return wiz_spi_transfer_byte(0xFF);
}

static void wiz_spi_writebyte(uint8_t data) {
    (void)wiz_spi_transfer_byte(data);
}

static void wiz_spi_readburst(uint8_t *buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = wiz_spi_readbyte();
    }
}

static void wiz_spi_writeburst(uint8_t *buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        (void)wiz_spi_transfer_byte(buf[i]);
    }
}

static void wizchip_hw_init(void) {
    gpio_init(WIZ_PIN_CS);
    gpio_set_dir(WIZ_PIN_CS, GPIO_OUT);
    gpio_put(WIZ_PIN_CS, 1);

    gpio_init(WIZ_PIN_SCK);
    gpio_set_dir(WIZ_PIN_SCK, GPIO_OUT);
    wiz_spi_set_sck(wiz_spi_cpol);

    gpio_init(WIZ_PIN_MOSI);
    gpio_set_dir(WIZ_PIN_MOSI, GPIO_OUT);
    gpio_put(WIZ_PIN_MOSI, 0);

    gpio_init(WIZ_PIN_MISO);
    gpio_set_dir(WIZ_PIN_MISO, GPIO_IN);
    gpio_pull_down(WIZ_PIN_MISO);

    gpio_init(WIZ_PIN_RST);
    gpio_set_dir(WIZ_PIN_RST, GPIO_OUT);

    gpio_init(WIZ_PIN_INT);
    gpio_set_dir(WIZ_PIN_INT, GPIO_IN);

    reg_wizchip_cs_cbfunc(wiz_cs_select, wiz_cs_deselect);
    reg_wizchip_spi_cbfunc(wiz_spi_readbyte, wiz_spi_writebyte);
    reg_wizchip_spiburst_cbfunc(wiz_spi_readburst, wiz_spi_writeburst);
}

static void wiz_reset_pulse(bool active_low) {
    wiz_reset_active_low = active_low;
    gpio_put(WIZ_PIN_RST, active_low ? 0 : 1);
    sleep_ms(10);
    gpio_put(WIZ_PIN_RST, active_low ? 1 : 0);
    sleep_ms(120);
}

static uint8_t wiz_raw_read_version(bool cpol, bool cpha) {
    uint8_t tx[4] = {0x00, 0x39, 0x00, 0x00};
    uint8_t rx[4] = {0, 0, 0, 0};

    wiz_spi_cpol = cpol;
    wiz_spi_cpha = cpha;
    wiz_spi_set_sck(wiz_spi_cpol);

    sleep_us(2);
    wiz_cs_select();
    for (int i = 0; i < 4; i++) {
        rx[i] = wiz_spi_transfer_byte(tx[i]);
    }
    wiz_cs_deselect();
    sleep_us(2);
    return rx[3];
}

static bool wiz_probe_comm(uint8_t *detected_version,
                           bool *detected_cpol,
                           bool *detected_cpha,
                           bool *detected_reset_active_low) {
    const struct {
        bool cpol;
        bool cpha;
        bool reset_active_low;
        const char *name;
    } attempts[] = {
        {false, false, true,  "mode0, rst active-low"},
        {false, false, false, "mode0, rst active-high"},
        {true,  true,  true,  "mode3, rst active-low"},
        {true,  true,  false, "mode3, rst active-high"},
    };

    for (size_t i = 0; i < (sizeof(attempts) / sizeof(attempts[0])); i++) {
        wiz_reset_pulse(attempts[i].reset_active_low);
        uint8_t version = wiz_raw_read_version(attempts[i].cpol, attempts[i].cpha);
        printf("W5500 probe %-24s -> VERSIONR 0x%02X\r\n", attempts[i].name, version);

        if (version == 0x04) {
            *detected_version = version;
            *detected_cpol = attempts[i].cpol;
            *detected_cpha = attempts[i].cpha;
            *detected_reset_active_low = attempts[i].reset_active_low;
            return true;
        }
    }

    return false;
}

static bool wizchip_init_and_check(uint8_t *tx_size,
                                   uint8_t *rx_size,
                                   bool cpol,
                                   bool cpha,
                                   bool reset_active_low,
                                   uint8_t *version_out) {
    wiz_spi_cpol = cpol;
    wiz_spi_cpha = cpha;
    wizchip_hw_init();
    wiz_reset_pulse(reset_active_low);
    wiz_spi_set_sck(wiz_spi_cpol);

    if (wizchip_init(tx_size, rx_size) != 0) {
        puts("W5500 init failed");
        return false;
    }

    uint8_t lib_version = getVERSIONR();
    uint8_t raw_version = wiz_raw_read_version(cpol, cpha);
    printf("W5500 post-init check: lib=0x%02X raw=0x%02X\r\n", lib_version, raw_version);

    *version_out = lib_version;
    return lib_version == 0x04;
}

static void print_network_info(void) {
    wiz_NetInfo current;
    ctlnetwork(CN_GET_NETINFO, &current);
    printf("Mode: %s\r\n", current.dhcp == NETINFO_DHCP ? "DHCP" : "STATIC");
    printf("MAC : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           current.mac[0], current.mac[1], current.mac[2],
           current.mac[3], current.mac[4], current.mac[5]);
    printf("IP  : %u.%u.%u.%u\r\n", current.ip[0], current.ip[1], current.ip[2], current.ip[3]);
    printf("SN  : %u.%u.%u.%u\r\n", current.sn[0], current.sn[1], current.sn[2], current.sn[3]);
    printf("GW  : %u.%u.%u.%u\r\n", current.gw[0], current.gw[1], current.gw[2], current.gw[3]);
    printf("DNS : %u.%u.%u.%u\r\n", current.dns[0], current.dns[1], current.dns[2], current.dns[3]);
}

static const char *phy_opmdc_to_text(uint8_t phycfgr) {
    switch (phycfgr & PHYCFGR_OPMDC_ALLA) {
    case PHYCFGR_OPMDC_ALLA:
        return "all capable auto";
    case PHYCFGR_OPMDC_PDOWN:
        return "power-down";
    case PHYCFGR_OPMDC_NA:
        return "not allowed";
    case PHYCFGR_OPMDC_100FA:
        return "100F/100H/10F/10H auto";
    case PHYCFGR_OPMDC_100F:
        return "100F";
    case PHYCFGR_OPMDC_100H:
        return "100H";
    case PHYCFGR_OPMDC_10F:
        return "10F";
    case PHYCFGR_OPMDC_10H:
        return "10H";
    default:
        return "reserved";
    }
}

static void print_phy_diagnostics(void) {
    uint8_t phycfgr = getPHYCFGR();
    const char *cfg_source = (phycfgr & PHYCFGR_OPMD) ? "software" : "hardware strap";
    const char *duplex = (phycfgr & PHYCFGR_DPX_FULL) ? "full" : "half";
    const char *speed = (phycfgr & PHYCFGR_SPD_100) ? "100M" : "10M";
    const char *link = (phycfgr & PHYCFGR_LNK_ON) ? "up" : "down";
    const char *reset_state = (phycfgr & (1u << 7)) ? "normal" : "reset asserted";

    printf("PHYCFGR: 0x%02X\r\n", phycfgr);
    printf("  reset=%s config=%s opmdc=%s speed=%s duplex=%s link=%s\r\n",
           reset_state,
           cfg_source,
           phy_opmdc_to_text(phycfgr),
           speed,
           duplex,
           link);
}

static void print_phy_link_status(void) {
    int8_t link = wizphy_getphylink();
    if (link < 0) {
        puts("PHY link status read failed");
        return;
    }
    printf("PHY link: %s\r\n", link ? "UP" : "DOWN");
    print_phy_diagnostics();
}

static bool ensure_phy_link_with_recovery(void) {
    int8_t link = wizphy_getphylink();
    if (link > 0) {
        return true;
    }

    puts("PHY recovery: enabling SW autoneg + PHY reset");
    wiz_PhyConf phyconf = {
        .by = PHY_CONFBY_SW,
        .mode = PHY_MODE_AUTONEGO,
        .speed = PHY_SPEED_100,
        .duplex = PHY_DUPLEX_FULL,
    };
    wizphy_setphyconf(&phyconf);

    for (int i = 0; i < 12; i++) {
        sleep_ms(250);
        link = wizphy_getphylink();
        if (link > 0) {
            puts("PHY recovery: link became UP");
            return true;
        }
    }

    puts("PHY recovery: link still DOWN");
    return false;
}

static void print_ip4(const char *label, const uint8_t ip[4]) {
    printf("%s%u.%u.%u.%u\r\n", label, ip[0], ip[1], ip[2], ip[3]);
}

static bool apply_and_verify_netinfo(const wiz_NetInfo *desired) {
    wiz_NetInfo verify;

    if (ctlnetwork(CN_SET_NETINFO, (void *)desired) != 0) {
        puts("CN_SET_NETINFO failed");
        return false;
    }

    if (ctlnetwork(CN_GET_NETINFO, &verify) != 0) {
        puts("CN_GET_NETINFO failed");
        return false;
    }

    if (verify.ip[0] != desired->ip[0] || verify.ip[1] != desired->ip[1] ||
        verify.ip[2] != desired->ip[2] || verify.ip[3] != desired->ip[3] ||
        verify.sn[0] != desired->sn[0] || verify.sn[1] != desired->sn[1] ||
        verify.sn[2] != desired->sn[2] || verify.sn[3] != desired->sn[3] ||
        verify.gw[0] != desired->gw[0] || verify.gw[1] != desired->gw[1] ||
        verify.gw[2] != desired->gw[2] || verify.gw[3] != desired->gw[3]) {
        puts("W5500 netinfo readback mismatch");
        print_ip4("  expected IP : ", desired->ip);
        print_ip4("  readback IP : ", verify.ip);
        print_ip4("  expected SN : ", desired->sn);
        print_ip4("  readback SN : ", verify.sn);
        print_ip4("  expected GW : ", desired->gw);
        print_ip4("  readback GW : ", verify.gw);
        return false;
    }

    return true;
}

static void dhcp_apply_netinfo(void) {
    getIPfromDHCP(net_info.ip);
    getSNfromDHCP(net_info.sn);
    getGWfromDHCP(net_info.gw);
    getDNSfromDHCP(net_info.dns);
    net_info.dhcp = NETINFO_DHCP;
    if (!apply_and_verify_netinfo(&net_info)) {
        puts("Failed to apply DHCP netinfo");
    }
    print_phy_link_status();
    print_network_info();
}

static void dhcp_ip_assign_cb(void) {
    puts("DHCP IP assigned");
    dhcp_apply_netinfo();
}

static void dhcp_ip_update_cb(void) {
    puts("DHCP IP updated");
    dhcp_apply_netinfo();
}

static void dhcp_ip_conflict_cb(void) {
    puts("DHCP IP conflict");
}

static bool ethernet_init(bool use_dhcp) {
    uint8_t tx_size[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    uint8_t rx_size[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    uint8_t probed_version = 0;
    uint8_t version = 0;
        bool selected_cpol = false;
        bool selected_cpha = false;
    bool selected_reset_active_low = true;
    bool link_ok = false;

    const struct {
        bool cpol;
        bool cpha;
        bool reset_active_low;
        const char *name;
    } attempts[] = {
        {false, false, true,  "mode0, rst active-low"},
        {false, false, false, "mode0, rst active-high"},
        {true,  true,  true,  "mode3, rst active-low"},
        {true,  true,  false, "mode3, rst active-high"},
    };

    wizchip_hw_init();

        printf("W5500 GPIO pins: CS=%u SCK=%u MISO=%u MOSI=%u RST=%u INT=%u\r\n",
            WIZ_PIN_CS,
            WIZ_PIN_SCK,
            WIZ_PIN_MISO,
            WIZ_PIN_MOSI,
            WIZ_PIN_RST,
            WIZ_PIN_INT);

    if (!wiz_probe_comm(&probed_version,
                        &selected_cpol,
                        &selected_cpha,
                   &selected_reset_active_low)) {
        puts("W5500 comm probe failed for all mode/reset combinations");
        return false;
    }

    printf("W5500 probe selected: cpol=%u cpha=%u reset_active_%s\r\n",
            selected_cpol ? 1u : 0u,
            selected_cpha ? 1u : 0u,
           selected_reset_active_low ? "low" : "high");

    if (wizchip_init_and_check(tx_size,
                               rx_size,
                               selected_cpol,
                               selected_cpha,
                               selected_reset_active_low,
                               &version)) {
        link_ok = true;
    } else {
        for (size_t i = 0; i < (sizeof(attempts) / sizeof(attempts[0])); i++) {
            if (attempts[i].cpol == selected_cpol &&
                attempts[i].cpha == selected_cpha &&
                attempts[i].reset_active_low == selected_reset_active_low) {
                continue;
            }

            printf("W5500 retry with %s\r\n", attempts[i].name);
            if (wizchip_init_and_check(tx_size,
                                       rx_size,
                                       attempts[i].cpol,
                                       attempts[i].cpha,
                                       attempts[i].reset_active_low,
                                       &version)) {
                selected_cpol = attempts[i].cpol;
                selected_cpha = attempts[i].cpha;
                selected_reset_active_low = attempts[i].reset_active_low;
                link_ok = true;
                break;
            }
        }
    }

    printf("W5500 final mode: cpol=%u cpha=%u reset_active_%s\r\n",
           selected_cpol ? 1u : 0u,
           selected_cpha ? 1u : 0u,
           selected_reset_active_low ? "low" : "high");
    printf("W5500 VERSIONR: 0x%02X\r\n", version);
    if (!link_ok || version != 0x04) {
        puts("W5500 comm check failed (expected VERSIONR=0x04)");
        return false;
    }

    (void)ensure_phy_link_with_recovery();

    if (use_dhcp) {
        net_info.dhcp = NETINFO_DHCP;
        if (ctlnetwork(CN_SET_NETINFO, &net_info) != 0) {
            puts("Failed to set DHCP mode");
            return false;
        }
        reg_dhcp_cbfunc(dhcp_ip_assign_cb, dhcp_ip_update_cb, dhcp_ip_conflict_cb);
        DHCP_init(DHCP_SOCKET, dhcp_buffer);
        puts("Ethernet initialized in DHCP mode");
    } else {
        net_info.dhcp = NETINFO_STATIC;
        if (!apply_and_verify_netinfo(&net_info)) {
            puts("Failed to apply static netinfo");
            return false;
        }
        puts("Ethernet initialized in STATIC mode");
        print_phy_link_status();
        print_network_info();
    }

    return true;
}

static void ethernet_task(void *arg) {
    (void)arg;

    bool use_dhcp = IP_MODE_DEFAULT_DHCP;

#if IP_MODE_SWITCH_ENABLE
    gpio_init(IP_MODE_SWITCH_PIN);
    gpio_set_dir(IP_MODE_SWITCH_PIN, GPIO_IN);
    gpio_pull_up(IP_MODE_SWITCH_PIN);

    bool pin_is_low = gpio_get(IP_MODE_SWITCH_PIN) == 0;
#if IP_MODE_SWITCH_ACTIVE_LOW
    use_dhcp = pin_is_low;
#else
    use_dhcp = !pin_is_low;
#endif
    printf("IP mode switch pin %u raw=%u -> %s\r\n",
           IP_MODE_SWITCH_PIN,
           pin_is_low ? 0u : 1u,
           use_dhcp ? "DHCP" : "STATIC");
#else
    printf("IP mode switch disabled -> %s\r\n", use_dhcp ? "DHCP" : "STATIC");
#endif

    if (!ethernet_init(use_dhcp)) {
        vTaskDelete(NULL);
        return;
    }

    uint32_t dhcp_elapsed_ms = 0;

    while (true) {
        if (use_dhcp) {
            uint8_t dhcp_state = DHCP_run();
            if (dhcp_state == 0) { // DHCP_FAILED in ioLibrary enum
                puts("DHCP failed - retrying");
                DHCP_init(DHCP_SOCKET, dhcp_buffer);
            }

            dhcp_elapsed_ms += 250;
            if (dhcp_elapsed_ms >= 1000) {
                DHCP_time_handler();
                dhcp_elapsed_ms = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static void heartbeat_task(void *arg) {
    (void)arg;
    while (true) {
        puts("VSCP demo W55RP20 running");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void) {
    stdio_init_all();
    stdio_uart_init_full(uart0, 115200, 0, 1);
    setvbuf(stdout, NULL, _IONBF, 0);
    sleep_ms(2000);
    puts("VSCP demo W55RP20 boot: serial debug enabled (USB+UART)");

    xTaskCreate(ethernet_task, "eth", 2048, NULL, 2, NULL);
    xTaskCreate(heartbeat_task, "beat", 1024, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true) {
    }
}
