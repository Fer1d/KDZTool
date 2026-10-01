#ifndef RAWPROGRAM_GENERATOR_HPP
#define RAWPROGRAM_GENERATOR_HPP

#include <string>

#include "diagnostics.hpp"
#include "dz_parser.hpp"

// Writes the rawprogram<N>.xml and patch<N>.xml files of a 9008/EDL (QFIL)
// flashing package. One <program> element is emitted per partition and points at
// the partition image the extractor wrote: "<hw_partition>.<name>.img".
// The patch files only carry entries when the GPT describes a partition that
// still grows to fill the disk.
void generate_rawprogram_files(const std::string& out_dir, const DzHeader& dz_hdr, Diagnostics& diag);

#endif // RAWPROGRAM_GENERATOR_HPP
