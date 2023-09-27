cd C:/Users/gsaee/Projects/openmw
rm -rfv C:/Users/gsaee/Projects/openmw/MSVC2022_64/Release
rm -rfv C:/Users/gsaee/Projects/openmw/MSVC2022_64/RelWithDebInfo
rm -rfv C:/Users/gsaee/Projects/openmw/MSVC2022_64/Debug
rm -rfv C:/Users/gsaee/Projects/openmw/MSVC2022_64/MinSizeRel
cd C:/Users/gsaee/Projects/openmw
rm CMakeCache.txt
./CI/before_script.msvc.sh -k -p Win64 -v 2022
