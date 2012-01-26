/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2012, Willow Garage
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/* Author: Ioan Sucan */

#include "ompl/base/StateStorage.h"
#include "ompl/base/PrecomputedStateSampler.h"
#include "ompl/util/Exception.h"
#include <boost/bind.hpp>
#include <fstream>

/// @cond IGNORE
static ompl::base::StateSamplerPtr allocPrecomputedStateSampler(const ompl::base::StateSpace *space,
                                                                const std::vector<int> &expectedSignature,
                                                                const std::vector<const ompl::base::State*> *states)
{
    std::vector<int> sig;
    space->computeSignature(sig);
    if (sig != expectedSignature)
    {
        std::stringstream ss;
        ss << "Cannot allocate state sampler for a state space whose signature does not match that of the stored states. ";
        ss << "Expected signature ";
        for (std::size_t i = 0 ; i < expectedSignature.size() ; ++i)
            ss << expectedSignature[i] << " ";
        ss << "but space " << space->getName() << " has signature ";
        for (std::size_t i = 0 ; i < sig.size() ; ++i)
            ss << sig[i] << " ";
        throw ompl::Exception(ss.str());
    }
    return ompl::base::StateSamplerPtr(new ompl::base::PrecomputedStateSampler(space, *states));
}

static const boost::uint32_t OMPL_ARCHIVE_MARKER = 0x4C504D4F; // this spells OMPL
/// @endcond

ompl::base::StateStorage::StateStorage(const StateSpacePtr &space) : space_(space)
{
}

ompl::base::StateStorage::~StateStorage(void)
{
    clear();
}

ompl::base::StateSamplerAllocator ompl::base::StateStorage::getStateSamplerAllocator(void) const
{
    std::vector<int> sig;
    space_->computeSignature(sig);
    return boost::bind(&allocPrecomputedStateSampler, _1, sig, &states_);
}

void ompl::base::StateStorage::load(const char *filename)
{
    std::ifstream in(filename, std::ios::binary);
    load(in);
    in.close();
}

bool ompl::base::StateStorage::loadHeader(std::istream &in, Header &header)
{
    if (!in.good() || in.eof())
    {
        msg_.warn("Unable to load states");
        return false;
    }

    // check marker
    boost::uint32_t marker = 0;
    in.read((char*)&marker, sizeof(uint32_t));
    if (marker != OMPL_ARCHIVE_MARKER)
    {
        msg_.error("The stored data does not start with the correct header");
        return false;
    }

    // check state space signature
    std::vector<int> sig;
    space_->computeSignature(sig);

    int signature_length = 0;
    if (in.good())
        in.read((char*)&signature_length, sizeof(int));
    if (sig[0] != signature_length)
    {
        msg_.error("State space signatures do not match");
        return false;
    }
    for (int i = 0 ; in.good() && i < signature_length ; ++i)
    {
        int s = 0;
        in.read((char*)&s, sizeof(int));
        if (s != sig[i + 1])
        {
            msg_.error("State space signatures do not match");
            return false;
        }
    }
    std::size_t state_count = 0;
    std::size_t metadata_size = 0;
    if (in.good())
        in.read((char*)&state_count, sizeof(std::size_t));
    else
    {
        msg_.error("Expected number of states. Incorrect file format");
        return false;
    }
    if (in.good())
        in.read((char*)&metadata_size, sizeof(std::size_t));
    else
    {
        msg_.error("Expected metadata size. Incorrect file format");
        return false;
    }
    if (state_count > 0 && !in.good())
    {
        msg_.error("Expected state data. Incorrect file format");
        return false;
    }
    header.state_count = state_count;
    header.metadata_size = metadata_size;
    return true;
}

void ompl::base::StateStorage::load(std::istream &in)
{
    clear();
    Header header;
    if (!loadHeader(in, header))
        return;

    // load the file
    unsigned int l = space_->getSerializationLength();
    std::size_t length = header.state_count * (l + header.metadata_size);
    char *buffer = length ? (char*)malloc(length) : NULL;
    if (buffer)
    {
        in.read(buffer, length);
        if (!in.fail())
        {
            msg_.debug("Deserializing %u states", header.state_count);
            states_.reserve(header.state_count);

            for (std::size_t i = 0 ; i < header.state_count ; ++i)
            {
                State *s = space_->allocState();
                space_->deserialize(s, buffer + i * (l + header.metadata_size));
                addState(s);
            }
        }
        else
            msg_.error("Unable to read state data. Incorrect file format");
        free(buffer);
    }
}

void ompl::base::StateStorage::store(const char *filename)
{
    std::ofstream out(filename, std::ios::binary);
    store(out);
    out.close();
}

void ompl::base::StateStorage::store(std::ostream &out)
{
    if (!out.good())
    {
        msg_.warn("Unable to store states");
        return;
    }

    // *************** begin write header information

    // write archive marker
    out.write((char*)&OMPL_ARCHIVE_MARKER, sizeof(boost::uint32_t));

    // write state space signature
    std::vector<int> sig;
    space_->computeSignature(sig);
    out.write((char*)&sig[0], sig.size() * sizeof(int));

    // write out the number of states
    std::size_t nr_states = states_.size();
    out.write((char*)&nr_states, sizeof(std::size_t));

    // write out the size of the metadata for each state (currently no metadata)
    std::size_t metadata_size = 0;
    out.write((char*)&metadata_size, sizeof(std::size_t));

    // *************** end write header information

    msg_.debug("Serializing %u states", (unsigned int)states_.size());

    unsigned int l = space_->getSerializationLength();
    std::size_t length = l * states_.size();
    char *buffer = (char*)malloc(length);
    for (std::size_t i = 0 ; i < states_.size() ; ++i)
        space_->serialize(buffer + i * l, states_[i]);
    out.write(buffer, length);
    free(buffer);
}

void ompl::base::StateStorage::addState(const State *state)
{
    states_.push_back(state);
}

void ompl::base::StateStorage::generateSamples(unsigned int count)
{
    StateSamplerPtr ss = space_->allocStateSampler();
    states_.reserve(states_.size() + count);
    for (unsigned int i = 0 ; i < count ; ++i)
    {
        State *s = space_->allocState();
        ss->sampleUniform(s);
        addState(s);
    }
}

void ompl::base::StateStorage::clear(void)
{
    for (std::size_t i = 0 ; i < states_.size() ; ++i)
        space_->freeState(const_cast<State*>(states_[i]));
    states_.clear();
}

void ompl::base::StateStorage::print(std::ostream &out) const
{
    for (std::size_t i = 0 ; i < states_.size() ; ++i)
        space_->printState(states_[i], out);
}
