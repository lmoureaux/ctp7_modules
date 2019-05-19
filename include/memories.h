#include "RPC.hpp"

/**
 * \brief An interface to memsvc.
 */
namespace Memories
{
    struct MemoriesBase : public RPC::Method
    {
        static constexpr char const * module = "memories";
    };

    /**
     * \brief Reads \c count words at the given \c address.
     */
    struct Read : public MemoriesBase
    {
        std::vector<std::uint32_t> operator()(std::uint32_t address,
                                              std::uint32_t count) const;
    };

    /**
     * \brief Writes \c data at the given \c address.
     */
    struct Write : public MemoriesBase
    {
        void operator()(std::uint32_t address,
                        const std::vector<std::uint32_t> &data) const;
    };
}
