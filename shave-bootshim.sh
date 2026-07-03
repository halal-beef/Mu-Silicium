unpack_bootimg --boot_img Mu-x1s-0.img
dd if=out/kernel of=silicium-nobootshim.bin bs=4k skip=112 iflag=skip_bytes
