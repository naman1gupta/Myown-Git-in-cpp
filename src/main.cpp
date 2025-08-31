#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <zlib.h>
#include <vector>
#include <iomanip>
#include <openssl/sha.h>
#include <algorithm>

struct TreeEntry {
    std::string mode;
    std::string name;
    std::string hash; // 20 bytes as hex string
};

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

std::vector<char> compressZlib(const std::string& data) {
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = data.size();
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));

    if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
        throw std::runtime_error("Failed to initialize zlib compression");
    }

    std::vector<char> result;
    char buffer[1024];
    
    do {
        strm.avail_out = sizeof(buffer);
        strm.next_out = reinterpret_cast<Bytef*>(buffer);
        
        int ret = deflate(&strm, Z_FINISH);
        if (ret == Z_STREAM_ERROR) {
            deflateEnd(&strm);
            throw std::runtime_error("Failed to compress zlib data");
        }
        
        result.insert(result.end(), buffer, buffer + (sizeof(buffer) - strm.avail_out));
    } while (strm.avail_out == 0);
    
    deflateEnd(&strm);
    return result;
}

std::string computeSHA1(const std::string& data) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(data.data()), data.length(), hash);
    
    std::stringstream ss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
    }
    return ss.str();
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

std::string writeGitObject(const std::string& content) {
    // Create the Git object format: "blob <size>\0<content>"
    std::string header = "blob " + std::to_string(content.length());
    std::string objectData = header + '\0' + content;
    
    // Compute SHA-1 hash of the uncompressed object data
    std::string hash = computeSHA1(objectData);
    
    // Compress the object data
    std::vector<char> compressedData = compressZlib(objectData);
    
    // Create directory structure
    std::string dir = ".git/objects/" + hash.substr(0, 2);
    std::filesystem::create_directories(dir);
    
    // Write compressed data to file
    std::string filename = dir + "/" + hash.substr(2);
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to create object file: " + filename);
    }
    
    file.write(compressedData.data(), compressedData.size());
    file.close();
    
    return hash;
}

std::string writeTreeObject(const std::vector<TreeEntry>& entries) {
    // Create the tree object content
    std::string treeContent;
    
    for (const auto& entry : entries) {
        // Convert hex hash back to raw bytes
        std::string rawHash;
        for (size_t i = 0; i < entry.hash.length(); i += 2) {
            std::string byteStr = entry.hash.substr(i, 2);
            rawHash += static_cast<char>(std::stoi(byteStr, nullptr, 16));
        }
        
        // Format: mode name\0hash
        treeContent += entry.mode + " " + entry.name + '\0' + rawHash;
    }
    
    // Create the Git object format: "tree <size>\0<content>"
    std::string header = "tree " + std::to_string(treeContent.length());
    std::string objectData = header + '\0' + treeContent;
    
    // Compute SHA-1 hash of the uncompressed object data
    std::string hash = computeSHA1(objectData);
    
    // Compress the object data
    std::vector<char> compressedData = compressZlib(objectData);
    
    // Create directory structure
    std::string dir = ".git/objects/" + hash.substr(0, 2);
    std::filesystem::create_directories(dir);
    
    // Write compressed data to file
    std::string filename = dir + "/" + hash.substr(2);
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to create tree object file: " + filename);
    }
    
    file.write(compressedData.data(), compressedData.size());
    file.close();
    
    return hash;
}

std::vector<TreeEntry> parseTreeObject(const std::string& objectData) {
    std::vector<TreeEntry> entries;
    size_t pos = 0;
    
    // Skip the header (type size\0)
    size_t nullPos = objectData.find('\0');
    if (nullPos == std::string::npos) {
        throw std::runtime_error("Invalid tree object format");
    }
    
    // Start parsing after the header
    pos = nullPos + 1;
    
    while (pos < objectData.length()) {
        // Find the next space (separates mode from name)
        size_t spacePos = objectData.find(' ', pos);
        if (spacePos == std::string::npos) {
            break;
        }
        
        // Extract mode
        std::string mode = objectData.substr(pos, spacePos - pos);
        
        // Find the null byte (separates name from hash)
        size_t nameEndPos = objectData.find('\0', spacePos);
        if (nameEndPos == std::string::npos) {
            break;
        }
        
        // Extract name
        std::string name = objectData.substr(spacePos + 1, nameEndPos - spacePos - 1);
        
        // Extract hash (20 bytes after the null byte)
        if (nameEndPos + 20 > objectData.length()) {
            break;
        }
        
        std::string rawHash = objectData.substr(nameEndPos + 1, 20);
        
        // Convert raw bytes to hex string
        std::stringstream ss;
        for (unsigned char byte : rawHash) {
            ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
        }
        std::string hash = ss.str();
        
        entries.push_back({mode, name, hash});
        
        // Move to next entry
        pos = nameEndPos + 21;
    }
    
    return entries;
}

std::string createTreeFromDirectory(const std::string& dirPath) {
    std::vector<TreeEntry> entries;
    
    // Iterate through directory entries
    for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
        std::string name = entry.path().filename().string();
        
        // Skip .git directory
        if (name == ".git") {
            continue;
        }
        
        if (entry.is_regular_file()) {
            // Create blob object for file
            std::ifstream file(entry.path());
            if (!file) {
                throw std::runtime_error("Failed to open file: " + entry.path().string());
            }
            
            std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
            file.close();
            
            std::string hash = writeGitObject(content);
            entries.push_back({"100644", name, hash}); // 100644 is regular file mode
            
        } else if (entry.is_directory()) {
            // Recursively create tree object for subdirectory
            std::string subTreeHash = createTreeFromDirectory(entry.path().string());
            entries.push_back({"40000", name, subTreeHash}); // 40000 is directory mode
        }
    }
    
    // Sort entries by name (Git requirement)
    std::sort(entries.begin(), entries.end(), 
             [](const TreeEntry& a, const TreeEntry& b) {
                 return a.name < b.name;
             });
    
    // Create and return tree object
    return writeTreeObject(entries);
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
    } else if (command == "hash-object") {
        if (argc < 4) {
            std::cerr << "Usage: hash-object -w <file>\n";
            return EXIT_FAILURE;
        }
        
        std::string flag = argv[2];
        std::string filename = argv[3];
        
        if (flag != "-w") {
            std::cerr << "Only -w flag is supported\n";
            return EXIT_FAILURE;
        }
        
        try {
            // Read the file content
            std::ifstream file(filename);
            if (!file) {
                std::cerr << "Failed to open file: " << filename << '\n';
                return EXIT_FAILURE;
            }
            
            std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
            file.close();
            
            // Create the blob object and get the hash
            std::string hash = writeGitObject(content);
            
            // Print the hash
            std::cout << hash << '\n';
            
        } catch (const std::exception& e) {
            std::cerr << "Error creating object: " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else if (command == "ls-tree") {
        if (argc < 4) {
            std::cerr << "Usage: ls-tree --name-only <tree>\n";
            return EXIT_FAILURE;
        }
        
        std::string flag = argv[2];
        std::string hash = argv[3];
        
        if (flag != "--name-only") {
            std::cerr << "Only --name-only flag is supported\n";
            return EXIT_FAILURE;
        }
        
        try {
            std::string objectData = readGitObject(hash);
            
            // Parse the tree object
            std::vector<TreeEntry> entries = parseTreeObject(objectData);
            
            // Sort entries by name (as Git does)
            std::sort(entries.begin(), entries.end(), 
                     [](const TreeEntry& a, const TreeEntry& b) {
                         return a.name < b.name;
                     });
            
            // Print just the names
            for (const auto& entry : entries) {
                std::cout << entry.name << '\n';
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error reading tree object: " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else if (command == "write-tree") {
        try {
            // Create tree object from current directory
            std::string hash = createTreeFromDirectory(".");
            
            // Print the hash
            std::cout << hash << '\n';
            
        } catch (const std::exception& e) {
            std::cerr << "Error creating tree: " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
