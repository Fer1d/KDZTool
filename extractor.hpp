#ifndef EXTRACTOR_HPP
#define EXTRACTOR_HPP

#include "kdz_parser.hpp"
#include "dz_parser.hpp"
#include "thread_pool.hpp"
#include <string>
#include <fstream>

void extract_kdz_components(std::ifstream& file, const KdzHeader& kdz_hdr, const std::string& out_path);
// flash_layout selects the layout of a 9008/EDL package: sparse partitions are
// written as one small file per chunk instead of one huge mostly empty image.
void extract_dz_parts(const std::string& in_path, const DzHeader& dz_hdr, const std::string& out_path,
                      ThreadPool& pool, bool flash_layout);
void extract_additional_data(std::ifstream& file, const KdzHeader& kdz_hdr, const std::string& out_path);

#endif // EXTRACTOR_HPP