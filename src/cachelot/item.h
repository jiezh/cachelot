#ifndef CACHELOT_CACHE_ITEM_H_INCLUDED
#define CACHELOT_CACHE_ITEM_H_INCLUDED

//
//  (C) Copyright 2015 Iurii Krasnoshchok
//
//  Distributed under the terms of Simplified BSD License
//  see LICENSE file


#ifndef CACHELOT_BYTES_H_INCLUDED
#  include <cachelot/bytes.h> // key, value
#endif
#ifndef CACHELOT_BITS_H_INCLUDED
#  include <cachelot/bits.h>  // unaligned_bytes
#endif
#ifndef CACHELOT_EXPIRATION_CLOCK_H_INCLUDED
#  include <cachelot/expiration_clock.h> // expiration time
#endif


namespace cachelot {

    namespace cache {

        /**
         * Item represents cache item with key, value, flags, cas, expiration, etc.
         *
         * For the sake of memory economy, Item has tricky placement in memory
         * @code
         * +---------+-------------------------+-------------------------------------?-------+----------?
         * |  Item   |  key as a sequence      |  value as a sequence                |[align]|[optional]|
         * |  struct |  of a[key_length] bytes |  of a [value_length] bytes          |[bytes]|[ CAS_val]|
         * +---------+-------------------------+-------------------------------------?-------+----------?
         * @endcode
         * Item structure is placed first after it
         * the key is placed, it's `key_length` bytes long then
         * item flags as ASCII string (the most of the time it will be '0')
         * the value bytes sequence
         * @ingroup cache
         */
        class Item {
        public:
            // There are primary type specifications
            // Modify with care! Memory Layout and Alignment!
            typedef uint32 hash_type;
            typedef uint16 opaque_flags_type;
            typedef ExpirationClock clock;
            typedef clock::time_point expiration_time_point;
            typedef uint64 timestamp_type;
            static constexpr uint8 max_key_length = 250; // ! key size is limited to uint8
            static constexpr uint32 max_value_length = std::numeric_limits<uint32>::max(); // ! value size is limited to uint32
        private:
            // Important! declaration order affects item size
            const timestamp_type m_timestamp; // timestamp of this item
            const hash_type m_hash; // hash value
            uint32 m_value_length; // length of value [0..MAX_VALUE_LENGTH]
            expiration_time_point m_expiration_time; // when it expires
            opaque_flags_type m_opaque_flags; // user defined item flags
            const uint8 m_key_length; // length of key [1..MAX_KEY_LENGTH]

        private:
            /// private destructor
            ~Item(); // should never be called

            // disallow copying / moving
            Item(const Item &) = delete;
            Item & operator= (const Item &) = delete;
            Item(Item &&) = delete;
            Item & operator= (Item &&) = delete;
        public:
            /// constructor
            explicit Item(bytes the_key, hash_type the_hash, uint32 value_length, opaque_flags_type the_flags, expiration_time_point expiration_time, timestamp_type the_timestamp) noexcept;

            /// Destroy existing Item
            static void Destroy(Item * item) noexcept;

            /// return bytes sequence occupied by key
            bytes key() const noexcept;

            /// return hash value
            hash_type hash() const noexcept { return m_hash; }

            /// return bytes sequence occupied by value
            bytes value() const noexcept;

            /// assign value to the item
            void assign_value(bytes the_value) noexcept;

            /// assign value from the two parts
            void assign_compose(bytes left, bytes right) noexcept;

            /// user defined flags
            opaque_flags_type opaque_flags() const noexcept { return m_opaque_flags; }

            /// retrieve timestamp of this item
            timestamp_type timestamp() const noexcept { return m_timestamp; }

            /// retrieve expiration time of this item
            expiration_time_point expiration_time() const noexcept { return m_expiration_time; }

            /// check whether Item is expired
            bool is_expired() const noexcept { return m_expiration_time <= clock::now(); }

            /// update item's expiration time
            void touch(expiration_time_point exptime) noexcept { m_expiration_time = exptime; }

            /// Calculate total size in bytes required to store provided fields
            static size_t CalcSizeRequired(const bytes the_key, const uint32 value_length) noexcept;

        private:
            // Item must be properly initialized to call following functions
            static size_t KeyOffset(const Item * i) noexcept;
            static size_t ValueOffset(const Item * i) noexcept;
        };


        inline Item::Item(bytes the_key, hash_type the_hash, uint32 value_length, opaque_flags_type the_flags, expiration_time_point expiration, timestamp_type the_timestamp) noexcept
                : m_timestamp(the_timestamp)
                , m_hash(the_hash)
                , m_value_length(value_length)
                , m_expiration_time(expiration)
                , m_opaque_flags(the_flags)
                , m_key_length(the_key.length()) {
            debug_assert(unaligned_bytes(this, alignof(Item) == 0));
            debug_assert(the_key.length() <= max_key_length);
            debug_assert(value_length <= max_value_length);
            auto this_ = reinterpret_cast<uint8 *>(this);
            std::memcpy(this_ + KeyOffset(this), the_key.begin(), the_key.length());
        }


        inline bytes Item::key() const noexcept {
            debug_assert(m_key_length > 0);
            auto key_begin = reinterpret_cast<const char *>(this) + KeyOffset(this);
            bytes k(key_begin, key_begin + m_key_length);
            return k;
        }


        inline bytes Item::value() const noexcept {
            auto value_begin = reinterpret_cast<const char *>(this) + ValueOffset(this);
            bytes v(value_begin, value_begin + m_value_length);
            return v;
        }


        inline void Item::assign_value(bytes the_value) noexcept {
            debug_assert(the_value.length() <= m_value_length);
            auto this_ = reinterpret_cast<uint8 *>(this);
            std::memcpy(this_ + ValueOffset(this), the_value.begin(), the_value.length());
            m_value_length = static_cast<decltype(m_value_length)>(the_value.length());
        }


        inline void Item::assign_compose(bytes left, bytes right) noexcept {
            debug_assert(left.length() + right.length() <= m_value_length);
            auto this_ = reinterpret_cast<uint8 *>(this);
            std::memcpy(this_ + ValueOffset(this), left.begin(), left.length());
            std::memcpy(this_ + ValueOffset(this) + left.length(), right.begin(), right.length());
            m_value_length = static_cast<decltype(m_value_length)>(left.length() + right.length());
        }


        inline size_t Item::KeyOffset(const Item *) noexcept {
            return sizeof(Item);
        }


        inline size_t Item::ValueOffset(const Item * i) noexcept {
            debug_assert(i != nullptr && i->m_key_length > 0);
            return KeyOffset(i) + i->m_key_length;
        }


        inline size_t Item::CalcSizeRequired(const bytes the_key, const uint32 value_length) noexcept {
            debug_assert(the_key.length() > 0);
            debug_assert(the_key.length() <= max_key_length);
            size_t item_size = sizeof(Item);
            item_size += the_key.length();
            item_size += value_length;
            return item_size;
        }

    } // namepsace cache

} // namespace cachelot


#endif // CACHELOT_CACHE_ITEM_H_INCLUDED
