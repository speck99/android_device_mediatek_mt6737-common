cd system/core
git reset --hard && git clean -f -d
patch -p1 < ../../device/mediatek/mt6737-common/patches/system_core_software_gatekeeper.patch
cd ../..
cd frameworks/base
git reset --hard && git clean -f -d
patch -p1 < ../../device/mediatek/mt6737-common/patches/frameworks_base_core_whitelist_proc_ged.patch
cd ../..
cd hardware/interfaces
git reset --hard && git clean -f -d
patch -p1 < ../../device/mediatek/mt6737-common/patches/0002-hardware_interfaces.patch
cd ../..
