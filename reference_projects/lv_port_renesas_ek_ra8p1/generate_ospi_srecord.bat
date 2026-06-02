echo "Project %1"

del %1.hex
del %1_no_OSPI_Data.srec
del OSPI_Data.srec
del OSPI_Data_swapped.srec


llvm-objcopy %1.elf -O srec -R __ospi0_cs1_readonly$$  %1_no_OSPI_Data.srec
llvm-objcopy -O srec -j __ospi0_cs1_readonly$$ %1.elf "OSPI_Data.srec"
..\srecord\srec_cat "OSPI_Data.srec" -byte_swap -o "OSPI_Data_swapped.srec"

del OSPI_Data.srec
