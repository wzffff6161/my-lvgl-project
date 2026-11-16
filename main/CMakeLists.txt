# 注意：SRC_DIRS 的源码列表在 CMake 配置时 glob 收集；
# 新增 .c 文件后需重新配置（touch 本文件即可）才会被纳入编译。
# 2026-10-16：新增 APP/icon/weather_bg.c（预烘焙毛玻璃背景图）。
# 2026-11-05：新增 APP/mine_logic.c、APP/mine_view.c（扫雷），触发重新 glob 收集源码。
# 2026-11-06：新增 APP/icon/Bomb.c、APP/icon/Flag.c（扫雷图标），触发重新 glob 收集源码。
idf_component_register(
    SRC_DIRS
        "."
        "APP"
        "APP/icon"
    INCLUDE_DIRS
        "."
        "APP"
)