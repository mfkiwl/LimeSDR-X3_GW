# Set the reference directory to where the script is
set script_path [file dirname [file normalize [info script]]]
puts $script_path

# Set output file path and name
set bit_path "[file normalize "$script_path/LimeSDR-X3/LimeSDR-X3.runs/impl_1/lms7_trx_top.bit"]"
puts $bit_path

set bit_string "up 0x00000000 $bit_path "
puts $bit_string

write_cfgmem  -format bin -force -size 16 -interface SPIx4 -loadbit $bit_string -file "[file normalize "$script_path/bitstream/flash_programming_file.bin"]"
file copy -force $bit_path $script_path/bitstream/ram_programming_file.bit
