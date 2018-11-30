cd system/core
git reset --hard && git clean -f -d
patch -p1 < ../../mediatek/mt6737-common/patches/Gatekeeperd-Add-use-software-gatekeeper-option.patch
cd ../..
cd hardware/interfaces
git reset --hard && git clean -f -d
patch -p1 < ../../device/mediatek/mt6737-common/patches/patches/HIDL-sensors-fallback-to-API_1_0.patch
patch -p1 < ../../device/mediatek/mt6737-common/patches/Error-message-suppress-option.patch
patch -p1 < ../../device/mediatek/mt6737-common/patches/Workaround-mtk-camera-blob.patch
patch -p1 < ../../device/mediatek/mt6737-common/patches/Disable-vndk-for-the-time-being.patch
cd ../..