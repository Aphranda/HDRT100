#include "ota_package.h"

#include "portable_ota_port.h"

bool ota_package_parse_header(const uint8_t *data,
                              uint32_t length,
                              ota_package_manifest_t *manifest)
{
    return portable_ota_port_parse_package_header(data, length, manifest);
}

const ota_package_image_t *ota_package_find_image(const ota_package_manifest_t *manifest,
                                                  ota_slot_t slot)
{
    return portable_ota_port_find_package_image(manifest, slot);
}
