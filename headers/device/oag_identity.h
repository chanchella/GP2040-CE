#pragma once

// Universal product identity for the OAG Vortex firmware.
//
// IMPORTANT:
// These strings are branding metadata only. Platform-specific VID/PID,
// protocol identifiers, security descriptors, authentication material,
// and compatibility-critical identities remain owned by their platform
// drivers and must not be replaced by these constants.

#define OAG_BRAND_NAME "OAG"
#define OAG_PRODUCT_NAME "OAG Vortex Ultra Gaming"
#define OAG_MANUFACTURER_NAME "OAG"

#define OAG_USB_MANUFACTURER_STRING OAG_MANUFACTURER_NAME
#define OAG_USB_PRODUCT_STRING OAG_PRODUCT_NAME

// Reserved for the future Bluetooth transport/profile.
#define OAG_BLUETOOTH_DEVICE_NAME OAG_PRODUCT_NAME

// Human-readable firmware family label for diagnostics/Web Config later.
#define OAG_FIRMWARE_FAMILY "OAG Vortex"
