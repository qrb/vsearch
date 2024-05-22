/*

  VSEARCH: a versatile open source tool for metagenomics

  Copyright (C) 2014-2024, Torbjorn Rognes, Frederic Mahe and Tomas Flouri
  All rights reserved.

  Contact: Torbjorn Rognes <torognes@ifi.uio.no>,
  Department of Informatics, University of Oslo,
  PO Box 1080 Blindern, NO-0316 Oslo, Norway

  This software is dual-licensed and available under a choice
  of one of two licenses, either under the terms of the GNU
  General Public License version 3 or the BSD 2-Clause License.


  GNU General Public License version 3

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.


  The BSD 2-Clause License

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
  COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

*/

#include "vsearch.h"
#include <algorithm>  // std::sort, std::min
#include <cstdio>  // std::FILE, std::fprintf
#include <cstdlib>  // std::ldiv
#include <cstring>  // std::strcmp
#include <vector>


struct sortinfo_length_s
{
  unsigned int length;
  unsigned int size;
  unsigned int seqno;
};


[[nodiscard]]
auto find_median_length(std::vector<sortinfo_length_s> const & sortinfo_v) -> double
{
  // function returns a round value or a value with a remainder of 0.5
  static constexpr double half = 0.5;

  if (sortinfo_v.empty()) {
    return 0.0;
  }

  // refactoring C++11: use const& std::vector.size()
  auto const midarray = std::ldiv(static_cast<long>(sortinfo_v.size()), 2L);

  // odd number of valid amplicons
  if (sortinfo_v.size() % 2 != 0)  {
    return sortinfo_v[midarray.quot].length * 1.0;  // a round value
  }

  // even number of valid amplicons
  // (average of two ints is either round or has a remainder of .5)
  // avoid risk of silent overflow for large abundance values:
  // a >= b ; (a + b) / 2 == b + (a - b) / 2
  return sortinfo_v[midarray.quot].length +
    ((sortinfo_v[midarray.quot - 1].length - sortinfo_v[midarray.quot].length) * half);
}


auto sortbylength() -> void
{
  if (opt_output == nullptr) {
    fatal("FASTA output file for sortbylength must be specified with --output");
    return;
  }

  auto * fp_output = fopen_output(opt_output);
  if (fp_output == nullptr) {
    fatal("Unable to open sortbylength output file for writing");
    return;
  }

  db_read(opt_sortbylength, 0);
  show_rusage();

  const auto dbsequencecount = db_getsequencecount();
  std::vector<struct sortinfo_length_s> sortinfo_v(dbsequencecount);

  auto passed = 0L;

  progress_init("Getting lengths", dbsequencecount);
  for(auto i = 0U; i < dbsequencecount; i++)
    {
      sortinfo_v[passed].seqno = i;
      sortinfo_v[passed].length = db_getsequencelen(i);
      sortinfo_v[passed].size = db_getabundance(i);
      passed++;
      progress_update(i);
    }
  progress_done();
  show_rusage();

  /* sort */
  auto compare_sequences = [](struct sortinfo_length_s const &lhs,
                              struct sortinfo_length_s const &rhs) -> bool {
    // longest first...
    if (lhs.length < rhs.length) {
      return false;
    }
    if (lhs.length > rhs.length) {
      return true;
    }
    // ... then ties are sorted by decreasing abundance values...
    if (lhs.size < rhs.size) {
      return false;
    }
    if (lhs.size > rhs.size) {
      return true;
    }
    // ...then ties are sorted by sequence labels (alpha-numerical ordering)...
    auto const result = std::strcmp(db_getheader(lhs.seqno), db_getheader(rhs.seqno));
    if (result > 0) {
      return false;
    }
    if (result < 0) {
      return true;
    }
    // ... then ties are sorted by input order (seqno)
    if (lhs.seqno < rhs.seqno) {
      return true;
    }
    return false;
  };

  static constexpr auto one_hundred_percent = 100ULL;
  progress_init("Sorting", one_hundred_percent);
  std::sort(sortinfo_v.begin(), sortinfo_v.end(), compare_sequences);
  progress_done();

  const double median = find_median_length(sortinfo_v);

  if (not opt_quiet)
    {
      fprintf(stderr, "Median length: %.0f\n", median);
    }

  if (opt_log != nullptr)
    {
      fprintf(fp_log, "Median length: %.0f\n", median);
    }

  show_rusage();

  // refactoring: std::min()
  passed = std::min(passed, opt_topn);
  sortinfo_v.resize(passed);
  sortinfo_v.shrink_to_fit();

  progress_init("Writing output", passed);
  auto counter = 0;
  for(auto const & sequence: sortinfo_v)
    {
      fasta_print_db_relabel(fp_output, sequence.seqno, counter + 1);
      progress_update(counter);
      ++counter;
    }
  progress_done();
  show_rusage();

  db_free();
  static_cast<void>(fclose(fp_output));
}
