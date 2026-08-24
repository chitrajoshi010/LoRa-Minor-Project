# LDSE forest-monitor — main component.
#
# Sources and INCLUDE_DIRS are selected by CONFIG_LDSE_ROLE so the gateway
# image (role 0) never compiles the classifier or sensors, while relay (1)
# and node (2) pull in the sensor stack and the acoustic classifier.
#
# REQUIRES/PRIV_REQUIRES, by contrast, is deliberately NOT role-conditional:
# ESP-IDF resolves component requirements during an "early expansion" CMake
# pass that runs before Kconfig options are available, so CONFIG_LDSE_ROLE
# (itself defined by this component's own Kconfig.projbuild) always reads as
# empty/unset at that point. Any `if(CONFIG_LDSE_ROLE EQUAL N)` guarding a
# REQUIRES entry silently falls through to the else() branch during that
# early pass and the "wrong" role's requirements get baked into the
# dependency graph permanently (e.g. esp_wifi would never be resolved for
# the gateway build even though the real per-role pass adds it to SRCS).
# The safe, standard ESP-IDF pattern is to list the union of all roles'
# REQUIRES unconditionally here; unused ones are simply harmless.

set(srcs
    "main.cpp"
    "ldse/EspHal.cpp"
    "ldse/LdseRadio.cpp"
    "ldse/LdseSync.cpp"
    "ldse/LdseRouting.cpp"
    "ldse/LdseForwarder.cpp"
    "ldse/LdseEnergy.cpp")

set(incdirs "." "ldse")

# Union of every role's REQUIRES (see note above for why this must not be
# role-conditional).
set(reqs
    jgromes__radiolib
    esp_timer
    esp_rom
    esp_driver_gpio
    esp_driver_spi
    driver
    esp_wifi
    esp_netif
    esp_event
    nvs_flash
    esp_adc
    esp_driver_i2s
    esp-tflite-micro
    espressif__esp-dsp
    esp_http_client
    esp-tls
    tcp_transport
    json)

if(CONFIG_LDSE_ROLE EQUAL 0)
    # Gateway: LDSE + Wi-Fi station (net/wifi_manager) + Firebase uploader.
    # No sensors/classifier.
    list(APPEND srcs "roles/gateway.cpp" "net/wifi_manager.cpp" "net/firebase_uploader.cpp")
    list(APPEND incdirs "net")
elseif(CONFIG_LDSE_ROLE EQUAL 1)
    # Relay: LDSE + sensors + fire scoring + acoustic classifier.
    list(APPEND srcs
        "roles/relay.cpp"
        "ldse/LdseSleepGate.cpp"
        "sensors/mq135.cpp"
        "sensors/dht22.cpp"
        "sensors/fire_scoring.cpp"
        "acoustic/audio_capture.cpp"
        "acoustic/spectrogram.cpp"
        "acoustic/classifier.cpp")
    list(APPEND incdirs "sensors" "acoustic")
else()
    # Node: LDSE + sensors + acoustic classifier.
    list(APPEND srcs
        "roles/node.cpp"
        "ldse/LdseSleepGate.cpp"
        "sensors/mq135.cpp"
        "sensors/dht22.cpp"
        "sensors/fire_scoring.cpp"
        "acoustic/audio_capture.cpp"
        "acoustic/spectrogram.cpp"
        "acoustic/classifier.cpp")
    list(APPEND incdirs "sensors" "acoustic")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${incdirs}
    REQUIRES ${reqs})
