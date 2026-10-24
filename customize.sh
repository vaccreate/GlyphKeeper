#!/system/bin/sh

# Проверка на KernelSU и наличие meta-overlayfs
if [ "$KSU" = "true" ] || [ "$KSU" = true ]; then
    if [ ! -d "/data/adb/modules/meta-overlayfs" ]; then
        ui_print "! Warning: You are using KernelSU."
        ui_print "! The 'glyphctl' binary in system/bin requires the 'meta-overlayfs' metamodule."
        ui_print "! Please install it, otherwise the CLI will not work."
    fi
fi

# Установка прав
set_perm $MODPATH/system/bin/glyphctl 0 0 0755
set_perm $MODPATH/service.sh 0 0 0755
set_perm $MODPATH/customize.sh 0 0 0755
set_perm $MODPATH/system/bin/flipd 0 0 0755
