# PlayStation Vita touchscreen (Syscon)
# The kernel driver reports MT protocol B positions in 0..1920 x 0..1080;
# Android's InputReader scales them to the 960x544 logical display.

touch.deviceType = touchScreen
touch.orientationAware = 1
device.internal = 1
