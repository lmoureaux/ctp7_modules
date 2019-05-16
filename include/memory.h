#include "RPC.hpp"

/**
 * \brief An interface to memsvc.
 */
namespace Memory
{
    /**
     * \brief Reads \c count words at the given \c address.
     */
    struct Read : public RPC::Method
    {
        static constexpr char * const name = "Read";
        std::vector<std::uint32_t> operator()(std::uint32_t address,
                                              std::uint32_t count) const;
    };

    /**
     * \brief Writes \c data at the given \c address.
     */
    struct Write : public RPC::Method
    {
        static constexpr char * const name = "Write";
        void operator()(std::uint32_t address,
                        const std::vector<std::uint32_t> &data) const;
    };
}
