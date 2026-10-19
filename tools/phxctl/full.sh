bash ~/gpu/bringup5.sh >/dev/null 2>&1
eval $SUDO_CMD bash -c "nohup shutdown -r +4 >/dev/null 2>&1 &"
eval $SUDO_CMD ~/gpu/phxctl/phxctl wreg 0xDE0 1 | tail -1
eval $SUDO_CMD ~/gpu/phxctl/phxctl wreg 0xA802 0 | tail -1
eval $SUDO_CMD ~/gpu/phxctl/phxctl wreg 0xA803 0x01000000 | tail -1
eval $SUDO_CMD ~/gpu/phxctl/mesboot gart
eval $SUDO_CMD ~/gpu/phxctl/mesboot mes
eval $SUDO_CMD env MESBOOT_MC=$MC MESBOOT_CS=$CS ~/gpu/phxctl/mesboot "$1" 2>&1 | tail -12
eval $SUDO_CMD killall shutdown 2>/dev/null
