cd system/core
git reset --hard && git clean -f -d
patch -p1 < ../../device/motorola/nicklaus/patches/Gatekeeperd-Add-use-software-gatekeeper-option.patch
cd ../..
cd hardware/interfaces
git reset --hard && git clean -f -d
patch -p1 < ../../device/mediatek/mt6737-common/patches/HIDL-sensors-fallback-to-API_1_0.patch
patch -p1 < ../../device/mediatek/mt6737-common/Error-message-suppress-option.patch
patch -p1 < ../../device/mediatek/mt6737-common/Workaround-mtk-camera-blob.patch
patch -p1 < ../../device/mediatek/mt6737-common/Disable-vndk-for-the-time-being.patch
cd ../..