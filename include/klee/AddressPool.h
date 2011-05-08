/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright (c) 2011, Dependable Systems Laboratory, EPFL
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * All contributors are listed in CLOUD9-AUTHORS file.
 *
*/

#ifndef ADDRESSPOOL_H_
#define ADDRESSPOOL_H_

#define ADDRESS_POOL_START      0xDEADBEEF00000000L // XXX This may very well overlap a mmap()-ed region
#define ADDRESS_POOL_SIZE       0x10000000
#define ADDRESS_POOL_GAP        (4*sizeof(uint64_t))
#define ADDRESS_POOL_ALIGN      (4*sizeof(uint64_t)) // Must be a power of two

namespace klee {

class AddressPool {
private:
  uint64_t startAddress;
  uint64_t size;


  uint64_t currentAddress;

  uint64_t gap;
  uint64_t align; // Must be a power of two
public:
  AddressPool(uint64_t _start, uint64_t _size) :
    startAddress(_start), size(_size),
    currentAddress(startAddress),
    gap(ADDRESS_POOL_GAP), align(ADDRESS_POOL_ALIGN) { }

  AddressPool() :
    startAddress(ADDRESS_POOL_START), size(ADDRESS_POOL_SIZE),
    currentAddress(startAddress),
    gap(ADDRESS_POOL_GAP), align(ADDRESS_POOL_ALIGN) { }

  virtual ~AddressPool() { }

  uint64_t allocate(uint64_t amount) {
    if (currentAddress + amount > startAddress + size)
      return 0;

    uint64_t result = currentAddress;

    currentAddress += amount + gap;

    if ((currentAddress & (align - 1)) != 0) {
      currentAddress = (currentAddress + align) & (~(align - 1));
    }

    return result;
  }

  uint64_t getStartAddress() const { return startAddress; }
  uint64_t getSize() const { return size; }
};

}

#endif /* ADDRESSPOOL_H_ */
