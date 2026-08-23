# LDSE forest-monitor — main component.
#
# Sources and managed-component requirements are selected by CONFIG_LDSE_ROLE
# so the gateway image (role 0) never compiles the classifier or sensors, while
# relay (1) and node (2) pull in the sensor stack and the acoustic classifier.

set(srcs
    "main.cpp"
    "ldse/LdseRadio.cpp"
    "ldse/LdseSync.cpp"
    "ldse/LdseRouting.cpp"
    "ldse/LdseForwarder.cpp"
    "ldse/LdseEnergy.cpp")

set(incdirs "." "ldse")
set(reqs RadioLib esp_timer driver)

if(CONFIG_LDSE_ROLE EQUAL 0)
    # Gateway: LDSE only.
    list(APPEND srcs "roles/gateway.cpp")
elseif(CONFIG_LDSE_ROLE EQUAL 1)
    # Relay: LDSE + sensors + fire scoring + acoustic classifier.
    list(APPEND srcs
        "roles/relay.cpp"
        "sensors/mq135.cpp"
        "sensors/dht22.cpp"
        "sensors/fire_scoring.cpp"
        "acoustic/audio_capture.cpp"
        "acoustic/spectrogram.cpp"
        "acoustic/classifier.cpp")
    list(APPEND incdirs "sensors" "acoustic")
    list(APPEND reqs esp_adc chmorgan__esp-dht esp_driver_i2s esp-tflite-micro espressif__esp-dsp)
else()
    # Node: LDSE + sensors + acoustic classifier.
    list(APPEND srcs
        "roles/node.cpp"
        "sensors/mq135.cpp"
        "sensors/dht22.cpp"
        "sensors/fire_scoring.cpp"
        "acoustic/audio_capture.cpp"
        "acoustic/spectrogram.cpp"
        "acoustic/classifier.cpp")
    list(APPEND incdirs "sensors" "acoustic")
    list(APPEND reqs esp_adc chmorgan__esp-dht esp_driver_i2s esp-tflite-micro espressif__esp-dsp)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${incdirs}
    REQUIRES ${reqs})
