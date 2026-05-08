#Create a directory to store the files
mkdir -p test_files
cd test_files

echo "Creating 10000 files with size from 1k to 10000k..."

#for i in {1..100}
for i in {1..10000}
do
    # Generate a random size between 1 and 10240 KB (10MB)
    SIZE=$(( ( RANDOM % 10240 ) + 1 ))
    
    echo "SIZE=${SIZE}"
    dd if=/dev/urandom of="dummy$i.bin" bs=1K count=$SIZE status=none
done

echo "Done! Created 10000 files in the 'test_files' directory."
