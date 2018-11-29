cd system/core
git reset --hard && git clean -f -d
git apply -v ../../device/mediatek/mt6737-common/patches/0001-system_core.patch
cd ../..
cd hardware/interfaces
git reset --hard && git clean -f -d
git apply -v ../../device/mediatek/mt6737-common/patches/0002-hardware_interfaces.patch
cd ../..
