/*
 * Copyright 2015 Follow My Vote, Inc.
 * This file is part of The Follow My Vote Stake-Weighted Voting Application ("SWV").
 *
 * SWV is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SWV is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SWV.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "FcStreamWrapper.hpp"

#include <fc/thread/thread.hpp>

#include <kj/debug.h>

namespace swv {

FcStreamWrapper::FcStreamWrapper(kj::Own<fc::iostream> wrappedStream)
    : wrappedStream(kj::mv(wrappedStream)) {}

FcStreamWrapper::~FcStreamWrapper() {
    writerHandle.cancel_and_wait(__FUNCTION__);
    readerHandle.cancel_and_wait(__FUNCTION__);
}

kj::Promise<void> FcStreamWrapper::write(const void* buffer, size_t size) {
    if (flushWrites)
        return KJ_EXCEPTION(DISCONNECTED, "write() called after shutdownWrite() has been called");
    auto promiseAndFulfiller = kj::newPromiseAndFulfiller<void>();

    pendingWrites.emplace(kj::mv(promiseAndFulfiller.fulfiller), buffer, size);
    startWrites();
    return kj::mv(promiseAndFulfiller.promise);
}

kj::Promise<void> FcStreamWrapper::write(kj::ArrayPtr<const kj::ArrayPtr<const kj::byte>> pieces) {
    if (flushWrites)
        return KJ_EXCEPTION(DISCONNECTED, "write() called after shutdownWrite() has been called");
    return kj::joinPromises(KJ_MAP(piece, pieces) {
                                return write(piece.begin(), piece.size());
                            });
}

kj::Promise<size_t> FcStreamWrapper::read(void* buffer, size_t minBytes, size_t maxBytes) {
    if (eof)
        return KJ_EXCEPTION(DISCONNECTED, "EOF when attempting to read", eof, minBytes);
    auto promiseAndFulfiller = kj::newPromiseAndFulfiller<size_t>();

    pendingReads.emplace(kj::mv(promiseAndFulfiller.fulfiller), buffer, minBytes, maxBytes, false);
    startReads();
    return kj::mv(promiseAndFulfiller.promise);
}

kj::Promise<size_t> FcStreamWrapper::tryRead(void* buffer, size_t minBytes, size_t maxBytes) {
    if (eof)
        return size_t(0);
    auto promiseAndFulfiller = kj::newPromiseAndFulfiller<size_t>();

    pendingReads.emplace(kj::mv(promiseAndFulfiller.fulfiller), buffer, minBytes, maxBytes, true);
    startReads();
    return kj::mv(promiseAndFulfiller.promise);
}

void FcStreamWrapper::shutdownWrite() {
    flushWrites = true;
    startWrites();
}

void FcStreamWrapper::startWrites() {
    // If there is not currently a fiber processing pending writes, queue one up
    if (!writerHandle.valid() || writerHandle.ready())
        writerHandle = fc::async([this] {processWrites();});
}

void FcStreamWrapper::startReads() {
    // If there is not currently a fiber processing pending reads, queue one up
    if (!readerHandle.valid() || readerHandle.ready())
        readerHandle = fc::async([this] {processReads();});
}

void FcStreamWrapper::processWrites() {
    while (!pendingWrites.empty()) {
        auto& currentWrite = pendingWrites.front();
        if (!eof) {
            wrappedStream->write(static_cast<const char*>(currentWrite.buffer), currentWrite.length);
            currentWrite.fulfiller->fulfill();
        } else {
            // When FC streams (at least, fc::tcp_sockets) read EOF, writing tends to hang forever. I attempted to
            // write anyways with fc::async and a timeout, but fc::future::wait(100ms) also hangs forever (!) when
            // I tried to write to the stream in the async call.
            currentWrite.fulfiller->reject(KJ_EXCEPTION(DISCONNECTED,
                                                        "Cowardly refusing to write to EOF'd FC stream."));
        }
        pendingWrites.pop();
    }

    if (flushWrites)
        wrappedStream->flush();
}

void FcStreamWrapper::processReads()
{
    while (!pendingReads.empty()) {
        auto& currentRead = pendingReads.front();
        size_t bytesRead = 0;

        auto reader = [this, &currentRead, &bytesRead] {
            // Keep reading until we have at least minBytes
            while (bytesRead < currentRead.minBytes) {
                // Ask for maxBytes -- readsome will give us all of them if they're available, or fewer if not.
                // It will only throw if it gets an EOF before the first byte is read.
                bytesRead += wrappedStream->readsome(static_cast<char*>(currentRead.buffer) + bytesRead,
                                                     currentRead.maxBytes - bytesRead);
            }
        };

        if (!eof && kj::runCatchingExceptions(kj::mv(reader)) == nullptr)
            // Nominal case -- everything is fine
            currentRead.fulfiller->fulfill(kj::mv(bytesRead));
        else {
            eof = true;
            if (currentRead.truncateForEof)
                currentRead.fulfiller->fulfill(kj::mv(bytesRead));
            else
                currentRead.fulfiller->reject(KJ_EXCEPTION(DISCONNECTED, "EOF when attempting to read",
                                                           bytesRead, currentRead.minBytes));
        }
        pendingReads.pop();
    }
}

} // namespace swv
