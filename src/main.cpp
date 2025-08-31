#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <zlib.h>
#include <vector>
#include <iomanip>

std::string decompressZlib(const std::vector<char>& compressedData) {
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = compressedData.size();
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressedData.data()));

    if (inflateInit(&strm) != Z_OK) {
        throw std::runtime_error("Failed to initialize zlib decompression");
    }

    std::string result;
    char buffer[1024];
    
    do {
        strm.avail_out = sizeof(buffer);
        strm.next_out = reinterpret_cast<Bytef*>(buffer);
        
        int ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&strm);
            throw std::runtime_error("Failed to decompress zlib data");
        }
        
        result.append(buffer, sizeof(buffer) - strm.avail_out);
    } while (strm.avail_out == 0);
    
    inflateEnd(&strm);
    return result;
}

std::string readGitObject(const std::string& hash) {
    // Git objects are stored as .git/objects/XX/YYYYYY... where XX is first 2 chars of hash
    std::string dir = ".git/objects/" + hash.substr(0, 2);
    std::string filename = dir + "/" + hash.substr(2);
    
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Object file not found: " + filename);
    }
    
    // Read entire file into vector
    std::vector<char> compressedData((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
    file.close();
    
    // Decompress the data
    return decompressZlib(compressedData);
}

int main(int argc, char *argv[])
{
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    std::cerr << "Logs from your program will appear here!\n";

    // Uncomment this block to pass the first stage
    //
    if (argc < 2) {
        std::cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }
    
    std::string command = argv[1];
    
    if (command == "init") {
        try {
            std::filesystem::create_directory(".git");
            std::filesystem::create_directory(".git/objects");
            std::filesystem::create_directory(".git/refs");
    
            std::ofstream headFile(".git/HEAD");
            if (headFile.is_open()) {
                headFile << "ref: refs/heads/main\n";
                headFile.close();
            } else {
                std::cerr << "Failed to create .git/HEAD file.\n";
                return EXIT_FAILURE;
            }
    
            std::cout << "Initialized git directory\n";
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else if (command == "cat-file") {
        if (argc < 4) {
            std::cerr << "Usage: cat-file -p <object>\n";
            return EXIT_FAILURE;
        }
        
        std::string flag = argv[2];
        std::string hash = argv[3];
        
        if (flag != "-p") {
            std::cerr << "Only -p flag is supported\n";
            return EXIT_FAILURE;
        }
        
        try {
            std::string objectData = readGitObject(hash);
            
            // Git object format: "type size\0content"
            size_t nullPos = objectData.find('\0');
            if (nullPos == std::string::npos) {
                std::cerr << "Invalid git object format\n";
                return EXIT_FAILURE;
            }
            
            std::string header = objectData.substr(0, nullPos);
            std::string content = objectData.substr(nullPos + 1);
            
            // For blob objects, just print the content
            std::cout << content;
            
        } catch (const std::exception& e) {
            std::cerr << "Error reading object: " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
