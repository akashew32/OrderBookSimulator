#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "csv_files.hpp"

namespace fs = std::filesystem;

int main() {
    fs::path root = fs::temp_directory_path() / "lob_csv_catalog_tests";
    fs::remove_all(root);
    fs::create_directories(root);

    fs::path valid = root / "valid.csv";
    {
        std::ofstream out(valid);
        out << "timestamp,event_type,order_id,side,type,price,quantity\n";
        out << "1,order,1,Buy,Limit,100,5\n";
        out << "3,order,2,Sell,Limit,101,4\n";
    }

    CsvFileCatalog catalog(root.string());
    std::string error;
    auto files = catalog.list(error);
    assert(error.empty());
    assert(files.size() == 1);
    assert(files[0].file_name == "valid.csv");
    assert(files[0].rows == 2);
    assert(files[0].start_timestamp == 1);
    assert(files[0].end_timestamp == 3);
    assert(!files[0].schema.empty());

    CsvMetadata uploaded;
    std::string content =
        "timestamp,event_type,order_id,side,type,price,quantity\n"
        "2,order,10,Buy,Limit,99,7\n";
    assert(catalog.save_upload("new_upload.csv", content, uploaded, error));
    assert(uploaded.file_name == "new_upload.csv");
    assert(uploaded.rows == 1);

    CsvMetadata bad;
    std::string bad_content = "event_type,order_id,side,type,price,quantity\norder,1,Buy,Limit,100,5\n";
    assert(!catalog.save_upload("bad.csv", bad_content, bad, error));
    assert(error.find("timestamp") != std::string::npos);

    auto after = catalog.list(error);
    assert(after.size() == 2);

    fs::remove_all(root);
    std::cout << "CSV file catalog tests passed!" << std::endl;
    return 0;
}
