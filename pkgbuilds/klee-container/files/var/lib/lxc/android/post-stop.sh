#!/bin/sh

if [ "$(systemctl get-default)" = "charger.target" ]; then
    # When running as a charger, hornor the charger binary's request to poweroff
    # or reboot.

    case "$LXC_TARGET" in
        stop)
            systemctl poweroff -f
            ;;
        reboot)
            systemctl reboot -f
            ;;
        *)
            # If we don't understand what happens, reboot.
            systemctl reboot -f
            ;;
    esac

    # The commands above doesn't block. To prevent the container from getting
    # restarted, return a failure.
    # https://github.com/lxc/lxc/pull/862
    exit 1
fi
