cd C:/Users/gsaee/Projects/openmw
rm -rfv C:/Users/gsaee/Projects/openmw/MSVC2022_64

cd C:/Users/gsaee/Projects/openmw
rm CMakeCache.txt
./CI/before_script.msvc.sh -k -p Win64 -v 2022
