#ifndef OSMIUM_IO_GZIP_COMPRESSION_HPP
#define OSMIUM_IO_GZIP_COMPRESSION_HPP

/*

This file is part of Osmium (https://osmcode.org/libosmium).

Copyright 2013-2020 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

/**
 * @file
 *
 * Include this file if you want to read or write gzip-compressed OSM
 * files.
 *
 * @attention If you include this file, you'll need to link with `libz`.
 */

#include <osmium/io/compression.hpp>
#include <osmium/io/detail/read_write.hpp>
#include <osmium/io/error.hpp>
#include <osmium/io/file_compression.hpp>
#include <osmium/io/writer_options.hpp>

#include <zlib.h>

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <limits>
#include <string>

#ifndef _MSC_VER
# include <unistd.h>
#endif

namespace osmium {

    /**
     * Exception thrown when there are problems compressing or
     * decompressing gzip files.
     */
    struct gzip_error : public io_error {

        int gzip_error_code = 0;
        int system_errno = 0;

        explicit gzip_error(const std::string& what) :
            io_error(what) {
        }

        gzip_error(const std::string& what, const int error_code) :
            io_error(what),
            gzip_error_code(error_code) {
            if (error_code == Z_ERRNO) {
                system_errno = errno;
            }
        }

    }; // struct gzip_error

    namespace io {

        namespace detail {

            [[noreturn]] inline void throw_gzip_error(gzFile gzfile, const char* msg) {
                std::string error{"gzip error: "};
                error += msg;
                error += ": ";
                int error_code = 0;
                if (gzfile) {
                    error += ::gzerror(gzfile, &error_code);
                }
                throw osmium::gzip_error{error, error_code};
            }

        } // namespace detail

        class GzipCompressor final : public Compressor {

            int m_fd;
            gzFile m_gzfile;

        public:

            explicit GzipCompressor(const int fd, const fsync sync) :
                Compressor(sync),
                m_fd(fd) {
#ifdef _MSC_VER
                osmium::detail::disable_invalid_parameter_handler diph;
#endif
                m_gzfile = ::gzdopen(osmium::io::detail::reliable_dup(fd), "wb");
                if (!m_gzfile) {
                    throw gzip_error{"gzip error: write initialization failed"};
                }
            }

            GzipCompressor(const GzipCompressor&) = delete;
            GzipCompressor& operator=(const GzipCompressor&) = delete;

            GzipCompressor(GzipCompressor&&) = delete;
            GzipCompressor& operator=(GzipCompressor&&) = delete;

            ~GzipCompressor() noexcept override {
                try {
                    close();
                } catch (...) {
                    // Ignore any exceptions because destructor must not throw.
                }
            }

            void write(const std::string& data) override {
#ifdef _MSC_VER
                osmium::detail::disable_invalid_parameter_handler diph;
#endif
                assert(m_gzfile);
                assert(data.size() < std::numeric_limits<unsigned int>::max());
                if (!data.empty()) {
                    const int nwrite = ::gzwrite(m_gzfile, data.data(), static_cast<unsigned int>(data.size()));
                    if (nwrite == 0) {
                        detail::throw_gzip_error(m_gzfile, "write failed");
                    }
                }
            }

            void close() override {
                if (m_gzfile) {
#ifdef _MSC_VER
                    osmium::detail::disable_invalid_parameter_handler diph;
#endif
                    const int result = ::gzclose_w(m_gzfile);
                    m_gzfile = nullptr;
                    if (result != Z_OK) {
                        throw gzip_error{"gzip error: write close failed", result};
                    }

                    // Do not sync or close stdout
                    if (m_fd == 1) {
                        return;
                    }

                    if (do_fsync()) {
                        osmium::io::detail::reliable_fsync(m_fd);
                    }
                    osmium::io::detail::reliable_close(m_fd);
                }
            }

        }; // class GzipCompressor

        class GzipDecompressor final : public Decompressor {

            gzFile m_gzfile = nullptr;

        public:

            explicit GzipDecompressor(const int fd) {
#ifdef _MSC_VER
                osmium::detail::disable_invalid_parameter_handler diph;
#endif
                m_gzfile = ::gzdopen(fd, "rb");
                if (!m_gzfile) {
                    try {
                        osmium::io::detail::reliable_close(fd);
                    } catch (...) {
                    }
                    throw gzip_error{"gzip error: read initialization failed"};
                }
            }

            GzipDecompressor(const GzipDecompressor&) = delete;
            GzipDecompressor& operator=(const GzipDecompressor&) = delete;

            GzipDecompressor(GzipDecompressor&&) = delete;
            GzipDecompressor& operator=(GzipDecompressor&&) = delete;

            ~GzipDecompressor() noexcept override {
                try {
                    close();
                } catch (...) {
                    // Ignore any exceptions because destructor must not throw.
                }
            }

            std::string read() override {
#ifdef _MSC_VER
                osmium::detail::disable_invalid_parameter_handler diph;
#endif
                assert(m_gzfile);
                std::string buffer(osmium::io::Decompressor::input_buffer_size, '\0');
                assert(buffer.size() < std::numeric_limits<unsigned int>::max());
                int nread = ::gzread(m_gzfile, &*buffer.begin(), static_cast<unsigned int>(buffer.size()));
                if (nread < 0) {
                    detail::throw_gzip_error(m_gzfile, "read failed");
                }
                buffer.resize(static_cast<std::string::size_type>(nread));
#if ZLIB_VERNUM >= 0x1240
                set_offset(static_cast<std::size_t>(::gzoffset(m_gzfile)));
#endif
                return buffer;
            }

            void close() override {
                if (m_gzfile) {
#ifdef _MSC_VER
                    osmium::detail::disable_invalid_parameter_handler diph;
#endif
                    const int result = ::gzclose_r(m_gzfile);
                    m_gzfile = nullptr;
                    if (result != Z_OK) {
                        throw gzip_error{"gzip error: read close failed", result};
                    }
                }
            }

        }; // class GzipDecompressor

        class GzipBufferDecompressor final : public Decompressor {

            const char* m_buffer;
            std::size_t m_buffer_size;
            z_stream m_zstream;

        public:

            GzipBufferDecompressor(const char* buffer, const std::size_t size) :
                m_buffer(buffer),
                m_buffer_size(size),
                m_zstream() {
                m_zstream.next_in = reinterpret_cast<unsigned char*>(const_cast<char*>(buffer));
                assert(size < std::numeric_limits<unsigned int>::max());
                m_zstream.avail_in = static_cast<unsigned int>(size);
                const int result = inflateInit2(&m_zstream, MAX_WBITS | 32); // NOLINT(hicpp-signed-bitwise)
                if (result != Z_OK) {
                    std::string message{"gzip error: decompression init failed: "};
                    if (m_zstream.msg) {
                        message.append(m_zstream.msg);
                    }
                    throw osmium::gzip_error{message, result};
                }
            }

            GzipBufferDecompressor(const GzipBufferDecompressor&) = delete;
            GzipBufferDecompressor& operator=(const GzipBufferDecompressor&) = delete;

            GzipBufferDecompressor(GzipBufferDecompressor&&) = delete;
            GzipBufferDecompressor& operator=(GzipBufferDecompressor&&) = delete;

            ~GzipBufferDecompressor() noexcept override {
                try {
                    close();
                } catch (...) {
                    // Ignore any exceptions because destructor must not throw.
                }
            }

            std::string read() override {
                std::string output;

                if (m_buffer) {
                    const std::size_t buffer_size = 10240;
                    output.append(buffer_size, '\0');
                    m_zstream.next_out = reinterpret_cast<unsigned char*>(&*output.begin());
                    m_zstream.avail_out = buffer_size;
                    const int result = inflate(&m_zstream, Z_SYNC_FLUSH);

                    if (result != Z_OK) {
                        m_buffer = nullptr;
                        m_buffer_size = 0;
                    }

                    if (result != Z_OK && result != Z_STREAM_END) {
                        std::string message{"gzip error: inflate failed: "};
                        if (m_zstream.msg) {
                            message.append(m_zstream.msg);
                        }
                        throw osmium::gzip_error{message, result};
                    }

                    output.resize(static_cast<std::size_t>(m_zstream.next_out - reinterpret_cast<const unsigned char*>(output.data())));
                }

                return output;
            }

            void close() override {
                inflateEnd(&m_zstream);
            }

        }; // class GzipBufferDecompressor

        namespace detail {

            // we want the register_compression() function to run, setting
            // the variable is only a side-effect, it will never be used
            const bool registered_gzip_compression = osmium::io::CompressionFactory::instance().register_compression(osmium::io::file_compression::gzip,
                [](const int fd, const fsync sync) { return new osmium::io::GzipCompressor{fd, sync}; },
                [](const int fd) { return new osmium::io::GzipDecompressor{fd}; },
                [](const char* buffer, const std::size_t size) { return new osmium::io::GzipBufferDecompressor{buffer, size}; }
            );

            // dummy function to silence the unused variable warning from above
            inline bool get_registered_gzip_compression() noexcept {
                return registered_gzip_compression;
            }

        } // namespace detail

    } // namespace io

} // namespace osmium

#endif // OSMIUM_IO_GZIP_COMPRESSION_HPP
