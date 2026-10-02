# Overlapping peripheral addresses in the nRF52840 devicetree (as in Zephyr's
# own nRF52840 boards)
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")
