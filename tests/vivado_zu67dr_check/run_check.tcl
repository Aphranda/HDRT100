set script_dir [file normalize [file dirname [info script]]]
set work_dir [file join $script_dir work]
set proj_dir [file join $work_dir zu67dr_license_check]

if {[file exists $work_dir]} {
    file delete -force $work_dir
}
file mkdir $work_dir

set preferred_part "xczu67dr-fsve1156-2-i"
set parts [get_parts -quiet $preferred_part]

if {[llength $parts] == 0} {
    set parts [lsort [get_parts -quiet xczu67dr*]]
}

if {[llength $parts] == 0} {
    puts "ZU67DR_CHECK_RESULT: FAIL no xczu67dr parts found in this Vivado installation"
    exit 2
}

set part [lindex $parts 0]
puts "ZU67DR_CHECK_PART: $part"

create_project zu67dr_license_check $proj_dir -part $part -force
add_files [file join $script_dir top.v]
set_property top top [current_fileset]
update_compile_order -fileset sources_1

synth_design -top top -part $part
opt_design
place_design
route_design
set_property SEVERITY Warning [get_drc_checks NSTD-1]
set_property SEVERITY Warning [get_drc_checks UCIO-1]
write_bitstream -force [file join $work_dir top.bit]

puts "ZU67DR_CHECK_RESULT: PASS bitstream generated"
exit 0
