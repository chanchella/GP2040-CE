from pathlib import Path

p = Path("joypad-os/src/apps/universal/app.c")
c = p.read_text()

marker = "    // Process button input\n    button_task();\n"
if marker not in c:
    raise SystemExit("marker not found")

diag = r'''
    // ------------------------------------------------------------------------
    // OAI diagnostic patch: phone-first BLE output proof, then BT input scan.
    // This lives only in the donor build wrapper; upstream JoypadOS source is
    // not modified in the repository.
    // ------------------------------------------------------------------------
#if REQUIRE_BLE_OUTPUT && REQUIRE_BT_INPUT
    {
        static bool diag_prev_ble = false;
        static bool diag_done = false;
        static uint32_t diag_started_ms = 0;
        static uint8_t diag_step = 0xFF;

        const bool diag_ble = ble_output_is_connected();
        const uint32_t diag_now_ms = (uint32_t)(time_us_64() / 1000ull);

        if (diag_ble && !diag_prev_ble && !diag_done) {
            diag_started_ms = diag_now_ms;
            diag_step = 0xFF;
            printf("[diag] BLE subscribed -> starting synthetic self-test\\n");
        }

        if (diag_ble && !diag_done) {
            const uint32_t elapsed = diag_now_ms - diag_started_ms;
            const uint8_t step = (uint8_t)(elapsed / 1200u);

            if (step < 6u && step != diag_step) {
                diag_step = step;

                input_event_t ev;
                init_input_event(&ev);
                ev.dev_addr = 0xF6;
                ev.instance = 0;
                ev.type = INPUT_TYPE_GAMEPAD;
                ev.transport = INPUT_TRANSPORT_GPIO;
                ev.layout = LAYOUT_MODERN_4FACE;

                if (step == 0u) {
                    ev.buttons = JP_BUTTON_B1;
                    printf("[diag] self-test: B1\\n");
                } else if (step == 2u) {
                    ev.buttons = JP_BUTTON_DD;
                    printf("[diag] self-test: D-pad Down\\n");
                } else if (step == 4u) {
                    ev.analog[ANALOG_LX] = 255;
                    printf("[diag] self-test: LX Right\\n");
                } else {
                    printf("[diag] self-test: neutral\\n");
                }

                router_submit_input(&ev);
            }

            if (step >= 6u) {
                input_event_t neutral;
                init_input_event(&neutral);
                neutral.dev_addr = 0xF6;
                neutral.instance = 0;
                neutral.type = INPUT_TYPE_GAMEPAD;
                neutral.transport = INPUT_TRANSPORT_GPIO;
                neutral.layout = LAYOUT_MODERN_4FACE;
                router_submit_input(&neutral);

                diag_done = true;
                bt_input_enabled = true;
                btstack_host_suppress_scan(false);
                btstack_host_start_scan();
                printf("[diag] self-test complete -> BT controller scan ENABLED\\n");
            }
        }

        diag_prev_ble = diag_ble;
    }
#endif
'''

c = c.replace(marker, marker + diag)
p.write_text(c)

print("JOYD_PHONE_FIRST_DIAG_PATCH=APPLIED")
