// Copyright (c) 2013, Cornell University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright notice,
//       this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of HyperDex nor the names of its contributors may be
//       used to endorse or promote products derived from this software without
//       specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// LevelDB
#include <hyperleveldb/write_batch.h>

// e
#include <e/endian.h>

// HyperDex
#include "daemon/datalayer_encodings.h"
#include "daemon/index_info.h"

using hyperdex::datalayer;

void
hyperdex :: encode_object_region(const region_id& ri,
                                 std::vector<char>* scratch,
                                 leveldb::Slice* out)
{
    size_t sz = sizeof(uint8_t) + sizeof(uint64_t);

    if (scratch->size() < sz)
    {
        scratch->resize(sz);
    }

    char* ptr = &scratch->front();
    *out = leveldb::Slice(ptr, sz);
    ptr = e::pack8be('o', ptr);
    ptr = e::pack64be(ri.get(), ptr);
}

void
hyperdex :: encode_key(const region_id& ri,
                       const e::slice& internal_key,
                       std::vector<char>* scratch,
                       leveldb::Slice* out)
{
    size_t sz = sizeof(uint8_t) + sizeof(uint64_t) + internal_key.size();

    if (scratch->size() < sz)
    {
        scratch->resize(sz);
    }

    char* ptr = &scratch->front();
    *out = leveldb::Slice(ptr, sz);
    ptr = e::pack8be('o', ptr);
    ptr = e::pack64be(ri.get(), ptr);
    memmove(ptr, internal_key.data(), internal_key.size());
}

void
hyperdex :: encode_key(const region_id& ri,
                       hyperdatatype key_type,
                       const e::slice& key,
                       std::vector<char>* scratch,
                       leveldb::Slice* out)
{
    index_info* ii(index_info::lookup(key_type));
    size_t sz = sizeof(uint8_t) + sizeof(uint64_t) + ii->encoded_size(key);

    if (scratch->size() < sz)
    {
        scratch->resize(sz);
    }

    char* ptr = &scratch->front();
    *out = leveldb::Slice(ptr, sz);
    ptr = e::pack8be('o', ptr);
    ptr = e::pack64be(ri.get(), ptr);
    ii->encode(key, ptr);
}

bool
hyperdex :: decode_key(const leveldb::Slice& in,
                       region_id* ri,
                       e::slice* internal_key)
{
    const size_t prefix_sz = sizeof(uint8_t) + sizeof(uint64_t);

    if (in.size() < prefix_sz ||
        in.data()[0] != 'o')
    {
        return false;
    }

    uint64_t r;
    e::unpack64be(in.data() + sizeof(uint8_t), &r);
    *ri = region_id(r);
    *internal_key = e::slice(in.data() + prefix_sz, in.size() - prefix_sz);
    return true;
}

void
hyperdex :: encode_value(const std::vector<e::slice>& attrs,
                         uint64_t version,
                         std::vector<char>* backing,
                         leveldb::Slice* out)
{
    assert(attrs.size() < 65536);
    size_t sz = sizeof(uint64_t) + sizeof(uint16_t);

    for (size_t i = 0; i < attrs.size(); ++i)
    {
        sz += sizeof(uint32_t) + attrs[i].size();
    }

    backing->resize(sz);
    char* ptr = &backing->front();
    ptr = e::pack64be(version, ptr);
    ptr = e::pack16be(attrs.size(), ptr);

    for (size_t i = 0; i < attrs.size(); ++i)
    {
        ptr = e::pack32be(attrs[i].size(), ptr);
        memmove(ptr, attrs[i].data(), attrs[i].size());
        ptr += attrs[i].size();
    }

    *out = leveldb::Slice(&backing->front(), sz);
}

datalayer::returncode
hyperdex :: decode_value(const e::slice& in,
                         std::vector<e::slice>* attrs,
                         uint64_t* version)
{
    const uint8_t* ptr = in.data();
    const uint8_t* end = ptr + in.size();

    if (ptr + sizeof(uint64_t) <= end)
    {
        ptr = e::unpack64be(ptr, version);
    }
    else
    {
        return datalayer::BAD_ENCODING;
    }

    uint16_t num_attrs;

    if (ptr + sizeof(uint16_t) <= end)
    {
        ptr = e::unpack16be(ptr, &num_attrs);
    }
    else
    {
        return datalayer::BAD_ENCODING;
    }

    attrs->clear();

    for (size_t i = 0; i < num_attrs; ++i)
    {
        uint32_t sz = 0;

        if (ptr + sizeof(uint32_t) <= end)
        {
            ptr = e::unpack32be(ptr, &sz);
        }
        else
        {
            return datalayer::BAD_ENCODING;
        }

        e::slice s(reinterpret_cast<const uint8_t*>(ptr), sz);
        ptr += sz;
        attrs->push_back(s);
    }

    return datalayer::SUCCESS;
}

void
hyperdex :: encode_acked(const region_id& ri, /*region we saw an ack for*/
                         const region_id& reg_id, /*region of the point leader*/
                         uint64_t seq_id,
                         char* out)
{
    char* ptr = out;
    ptr = e::pack8be('a', ptr);
    ptr = e::pack64be(reg_id.get(), ptr);
    ptr = e::pack64be(seq_id, ptr);
    ptr = e::pack64be(ri.get(), ptr);
}

datalayer::returncode
hyperdex :: decode_acked(const e::slice& in,
                         region_id* ri, /*region we saw an ack for*/
                         region_id* reg_id, /*region of the point leader*/
                         uint64_t* seq_id)
{
    if (in.size() != ACKED_BUF_SIZE)
    {
        return datalayer::BAD_ENCODING;
    }

    uint8_t _p;
    uint64_t _ri;
    uint64_t _reg_id;
    const uint8_t* ptr = in.data();
    ptr = e::unpack8be(ptr, &_p);
    ptr = e::unpack64be(ptr, &_reg_id);
    ptr = e::unpack64be(ptr, seq_id);
    ptr = e::unpack64be(ptr, &_ri);
    *ri = region_id(_ri);
    *reg_id = region_id(_reg_id);
    return _p == 'a' ? datalayer::SUCCESS : datalayer::BAD_ENCODING;
}

void
hyperdex :: encode_checkpoint(const region_id& ri,
                              uint64_t checkpoint,
                              char* out)
{
    char* ptr = out;
    ptr = e::pack8be('c', ptr);
    ptr = e::pack64be(ri.get(), ptr);
    ptr = e::pack64be(checkpoint, ptr);
}

datalayer::returncode
hyperdex :: decode_checkpoint(const e::slice& in,
                              region_id* ri,
                              uint64_t* checkpoint)
{
    if (in.size() != CHECKPOINT_BUF_SIZE)
    {
        return datalayer::BAD_ENCODING;
    }

    const uint8_t* ptr = in.data();
    uint8_t t;
    uint64_t _ri;
    ptr = e::unpack8be(ptr, &t);
    ptr = e::unpack64be(ptr, &_ri);
    ptr = e::unpack64be(ptr, checkpoint);
    *ri = region_id(_ri);
    return t == 'c' ? datalayer::SUCCESS : datalayer::BAD_ENCODING;
}

void
hyperdex :: create_index_changes(const schema& sc,
                                 const subspace& sub,
                                 const region_id& ri,
                                 const e::slice& key,
                                 const std::vector<e::slice>* old_value,
                                 const std::vector<e::slice>* new_value,
                                 leveldb::WriteBatch* updates)
{
    assert(!old_value || !new_value || old_value->size() == new_value->size());
    assert(!old_value || old_value->size() + 1 == sc.attrs_sz);
    assert(!new_value || new_value->size() + 1 == sc.attrs_sz);
    std::vector<uint16_t> attrs;

    for (size_t i = 0; i < sub.attrs.size(); ++i)
    {
        uint16_t attr = sub.attrs[i];
        attrs.push_back(attr);
    }

    for (size_t i = 0; i < sub.indices.size(); ++i)
    {
        uint16_t attr = sub.indices[i];
        attrs.push_back(attr);
    }

    for (size_t i = 0; i < attrs.size(); ++i)
    {
        uint16_t attr = attrs[i];
        assert(attr < sc.attrs_sz);

        if (attr == 0)
        {
            continue;
        }

        if (!sub.indexed(attr))
        {
            continue;
        }

        index_info* ki = index_info::lookup(sc.attrs[0].type);
        index_info* ai = index_info::lookup(sc.attrs[attr].type);

        if (!ai)
        {
            continue;
        }

        assert(ki);
        ai->index_changes(ri, attr, ki, key,
                          old_value ? &(*old_value)[attr - 1] : NULL,
                          new_value ? &(*new_value)[attr - 1] : NULL,
                          updates);
    }
}

void
hyperdex :: encode_bump(char* _ptr, char* _end)
{
    assert(_ptr);
    assert(_ptr < _end);
    uint8_t* ptr = reinterpret_cast<uint8_t*>(_end) - 1;
    uint8_t* end = reinterpret_cast<uint8_t*>(_ptr);

    for (; ptr >= end; --ptr)
    {
        if (*ptr < 255)
        {
            ++(*ptr);
            return;
        }
        else
        {
            *ptr = 0;
        }
    }

    abort();
}
