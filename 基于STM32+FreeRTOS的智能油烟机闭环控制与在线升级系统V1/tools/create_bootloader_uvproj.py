#!/usr/bin/env python3
"""Generate the independent Keil Bootloader target from the application project."""

from copy import deepcopy
from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Project.uvprojx"
OUTPUT = ROOT / "Bootloader" / "Bootloader.uvprojx"


GROUPS = {
    "Bootloader": [
        ("boot_main.c", 1, ".\\boot_main.c"),
        ("boot_sha256.c", 1, ".\\boot_sha256.c"),
        ("boot_p256.c", 1, ".\\boot_p256.c"),
        ("boot_crypto.c", 1, ".\\boot_crypto.c"),
        ("boot_flash.c", 1, ".\\boot_flash.c"),
        ("boot_metadata.c", 1, ".\\boot_metadata.c"),
    ],
    "Common": [
        ("ota_protocol.c", 1, "..\\Common\\ota_protocol.c"),
        ("ota_boot_control.c", 1, "..\\Common\\ota_boot_control.c"),
        ("ota_config.h", 5, "..\\Common\\ota_config.h"),
        ("ota_public_key.h", 5, "..\\Common\\ota_public_key.h"),
    ],
    "Start": [
        ("startup_stm32f10x_hd.s", 2, "..\\Start\\startup_stm32f10x_hd.s"),
        ("system_stm32f10x.c", 1, "..\\Start\\system_stm32f10x.c"),
        ("core_cm3.c", 1, "..\\Start\\core_cm3.c"),
    ],
    "STM32 Library": [
        ("stm32f10x_rcc.c", 1, "..\\Library\\stm32f10x_rcc.c"),
        ("stm32f10x_gpio.c", 1, "..\\Library\\stm32f10x_gpio.c"),
        ("stm32f10x_usart.c", 1, "..\\Library\\stm32f10x_usart.c"),
        ("stm32f10x_flash.c", 1, "..\\Library\\stm32f10x_flash.c"),
        ("stm32f10x_pwr.c", 1, "..\\Library\\stm32f10x_pwr.c"),
        ("stm32f10x_bkp.c", 1, "..\\Library\\stm32f10x_bkp.c"),
    ],
}


def set_text(parent: ET.Element, path: str, value: str) -> None:
    element = parent.find(path)
    if element is None:
        raise RuntimeError(f"missing project element: {path}")
    element.text = value


def main() -> None:
    tree = ET.parse(SOURCE)
    root = tree.getroot()
    targets = root.find("Targets")
    if targets is None or not list(targets):
        raise RuntimeError("application project has no target")
    target = deepcopy(list(targets)[0])
    targets.clear()
    targets.append(target)

    set_text(target, "TargetName", "Bootloader")
    common = target.find("TargetOption/TargetCommonOption")
    assert common is not None
    set_text(common, "Cpu", 'IRAM(0x20000000,0xC000) IROM(0x08000000,0x8000) CPUTYPE("Cortex-M3") CLOCK(12000000) ELITTLE')
    set_text(common, "OutputDirectory", ".\\Objects\\")
    set_text(common, "OutputName", "Bootloader")
    set_text(common, "ListingPath", ".\\Objects\\")
    set_text(common, "CreateHexFile", "1")

    arm_misc = target.find("TargetOption/TargetArmAds/ArmAdsMisc")
    assert arm_misc is not None
    for memory in arm_misc.findall("OnChipMemories/IROM") + arm_misc.findall("OnChipMemories/OCR_RVCT4"):
        set_text(memory, "StartAddress", "0x8000000")
        set_text(memory, "Size", "0x8000")

    cads = target.find("TargetOption/TargetArmAds/Cads/VariousControls")
    assert cads is not None
    set_text(cads, "Define", "STM32F10X_HD,USE_STDPERIPH_DRIVER,VECT_TAB_OFFSET=0")
    set_text(cads, "IncludePath", ".;..\\Start;..\\User;..\\Library;..\\Common;..\\BSP")
    ldads = target.find("TargetOption/TargetArmAds/LDads")
    assert ldads is not None
    set_text(ldads, "umfTarg", "0")
    set_text(ldads, "useFile", "1")
    set_text(ldads, "TextAddressRange", "0x08000000")
    set_text(ldads, "ScatterFile", ".\\Bootloader.sct")

    after_make = common.find("AfterMake")
    assert after_make is not None
    set_text(after_make, "RunUserProg1", "1")
    set_text(after_make, "UserProg1Name", "fromelf.exe --bin --output .\\Objects\\Bootloader.bin .\\Objects\\Bootloader.axf")

    groups_element = target.find("Groups")
    assert groups_element is not None
    groups_element.clear()
    for group_name, files in GROUPS.items():
        group = ET.SubElement(groups_element, "Group")
        ET.SubElement(group, "GroupName").text = group_name
        files_element = ET.SubElement(group, "Files")
        for filename, filetype, filepath in files:
            file_element = ET.SubElement(files_element, "File")
            ET.SubElement(file_element, "FileName").text = filename
            ET.SubElement(file_element, "FileType").text = str(filetype)
            ET.SubElement(file_element, "FilePath").text = filepath

    ET.indent(tree, space="  ")
    tree.write(OUTPUT, encoding="UTF-8", xml_declaration=True)
    print(OUTPUT)


if __name__ == "__main__":
    main()
