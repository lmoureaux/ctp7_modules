#ifndef RPC_HPP
#define RPC_HPP

#include <memory>
#include <type_traits>
#include <utility>
#include <tuple>

// Supported types
#include <cstdint>
#include <string>
#include <vector>

#include "rpc/wiscrpcsvc.h"
#include "rpc/wiscRPCMsg.h"
// #include "LogManager.h" FIXME Use only on remote side

#include <execinfo.h> // Backtrace

namespace RPC {

    /*
     * For multiples reasons (among others not having access to C++14 and C++17),
     * we need some helpers.
     */
    namespace helpers {

        /*
         * Get the return type and the arguments of a functor.
         *
         * Inspired by https://stackoverflow.com/a/10400131
         */
        template<typename> struct functor_traits;

        template<typename Obj,
                 typename R,
                 typename... Args
                >
        struct functor_traits<R (Obj::*)(Args...) const>
        {
            using return_type = R;
            using args_type = std::tuple<Args...>;
            using decay_args_type = std::tuple<typename std::decay<Args>::type...>;
        };

        template<typename Func,
                 typename Traits = functor_traits<Func>,
                 typename R = typename Traits::return_type
                >
        R functor_return_t_impl(Func);

        template <typename Obj>
            using functor_return_t = decltype(functor_return_t_impl(&Obj::operator()));

        template<typename Func,
                 typename Traits = functor_traits<Func>,
                 typename Args = typename Traits::args_type
                >
        Args functor_args_t_impl(Func);

        template <typename Obj>
            using functor_args_t = decltype(functor_args_t_impl(&Obj::operator()));

        template<typename Func,
                 typename Traits = functor_traits<Func>,
                 typename Args = typename Traits::decay_args_type
                >
        Args functor_decay_args_t_impl(Func);

        template <typename Obj>
            using functor_decay_args_t = decltype(functor_decay_args_t_impl(&Obj::operator()));

        /*
         * Check if T is a std::tuple.
         */
        template<typename T>
            struct is_tuple_impl : std::false_type {};

        template<typename... T>
            struct is_tuple_impl<std::tuple<T...>> : std::true_type {};

        template<typename T>
            using is_tuple = is_tuple_impl<typename std::decay<T>::type>;

        /*
         * Define an indices sequence.
         */
        template<std::size_t... I> struct index_sequence {};

        /*
         * Produce an indices sequence.
         *
         * Usual O(n) implementation.
         * See https://blog.galowicz.de/2016/06/24/integer_sequences_at_compile_time/
         * for reference.
         */
        template<std::size_t... N> struct index_sequence_gen;

        // `I` is the recursion index while `N...` are the generated indices.
        template<std::size_t I, std::size_t... N> struct index_sequence_gen<I, N...>
        {
            using type = typename index_sequence_gen<I-1, I-1, N...>::type;
        };

        // Terminal call
        template<std::size_t... N> struct index_sequence_gen<0, N...>
        {
            using type = index_sequence<N...>;
        };

        template<std::size_t N>
            using make_index_sequence = typename index_sequence_gen<N>::type;

        /*
         * This structure allows to use `void` as a parameter in overloads.
         * In order to work, the real object is encapsulated into the `void_holder`
         * which and can then be retrieved.
         */
        template<typename T> struct void_holder
        {
            const T t;
            const T &get() const { return t; }
        };

        template<> struct void_holder<void> { /* Nothing to do. */ };

        /*
         * Call a function with arguments coming from a std::tuple.
         * In order to handle the `void` return case, the first template
         * parameter must be explicitly set.
         */
        template<typename F,
                 typename... Args,
                 std::size_t... I
                >
        auto tuple_apply_impl(F &&f, const std::tuple<Args...> &tuple, index_sequence<I...>)
            -> decltype(std::forward<F>(f)(std::get<I>(tuple)...))
        {
            return std::forward<F>(f)(std::get<I>(tuple)...);
        }

        template<typename R,
                 typename F,
                 typename... Args,
                 typename std::enable_if<std::is_void<R>::value, int>::type = 0
                >
        void_holder<void> tuple_apply(F &&f, const std::tuple<Args...> &tuple)
        {
            tuple_apply_impl(std::forward<F>(f), tuple, make_index_sequence<sizeof...(Args)>{});
            return {};
        }

        template<typename R,
                 typename F,
                 typename... Args,
                 typename std::enable_if<!std::is_void<R>::value, int>::type = 0
                >
        void_holder<R> tuple_apply(F &&f, const std::tuple<Args...> &tuple)
        {
            return void_holder<R>{
                tuple_apply_impl(std::forward<F>(f), tuple, make_index_sequence<sizeof...(Args)>{})
            };
        }

    } // namespace helpers
    using namespace helpers;

    /*
     * Inherit from this class to define a new RPC method.
     */
    struct Method
    {
        constexpr static const char *name = "";
        constexpr static uint32_t revision = 0;

        // This method is not needed but emphasize the need to define operator()
        template<typename R, typename... Args> R operator()(Args...) const;
    };

    /*
     * Type to/from RPC message serializer/deserializer.
     *
     * Specialize this class to add new serializable types.
     * You must specialize it without cv-qualifier since the Message
     * class takes care of decaying the types.
     */

    // Forward reference
    class Message;

    template<typename T>
    struct Serializer
    {
        static void from(Message &, T) = delete;
        static T to(Message &) = delete;
    };

    /*
     * We handle immediately the special `void` case here.
     * It is an incomplete type with special rules so is our code.
     */
    template<> struct Serializer<void>
    { static void to(Message &) { /* Nothing to do. */ } };

    /*
     * RPC Message
     */
    class Message
    {
            union {
                const wisc::RPCMsg *read;
                wisc::RPCMsg *write;
            } _wisc;

        protected:
            /*
             * Index of the next free/unread key.
             */
            uint32_t _key_idx = 0;

            // The serializer is a friend so that it has access to the key index
            // Could write a "key dispenser method" ?
            template<typename T> friend struct Serializer;

            /*
             * Used by Serializers to fetch keys from the wiscRPCMsg.
             */
            template<typename T> T get_key(int index) const;

            /*
             * Used by Serializers to write keys to the wiscRPCMsg.
             */
            void set_key(int index, std::uint32_t value);

            /*
             * Used by Serializers to write keys to the wiscRPCMsg.
             */
            void set_key(int index, const std::vector<std::uint32_t> &value);

            /*
             * Used by Serializers to write keys to the wiscRPCMsg.
             */
            void set_key(int index, const std::string &value);

            /*
             * Used by Serializers to write keys to the wiscRPCMsg.
             */
            void set_key(int index, const std::vector<std::string> &value);

        public:
            /*
             * Constructor.
             */
            explicit Message(const wisc::RPCMsg *read) noexcept : _wisc{read} {}

            /*
             * Constructor.
             */
            explicit Message(wisc::RPCMsg *write) noexcept : _wisc{write} {}

            /*
             * Set a single key from a known type.
             */
            template<typename T> void set(T t)
            { Serializer<typename std::decay<T>::type>::from(*this, t); }

            /*
             * Set a sinle key from holder.
             * Should be used only when setting the result from a function.
             */
            template<typename T> void set(const void_holder<T> &holder)
            { Serializer<typename std::decay<T>::type>::from(*this, holder.get()) ;}

            void set(void_holder<void>) { /* Nothing to do. */ }

            /*
             * Recursively set keys from a tuple.
             * Used for the function parameters.
             *
             * Parameters are added from left to right.
             */
            template<std::size_t N = 0,
                     typename... Args,
                     typename std::enable_if<N < sizeof...(Args), int>::type = 0
                    >
            void set(const std::tuple<Args...> &args)
            {
                Serializer<typename std::decay<
                        typename std::tuple_element<N, std::tuple<Args...>>::type>
                    ::type>::from(*this, std::get<N>(args));

                this->set<N+1>(args);
            }

            template<std::size_t N = 0,
                     typename... Args,
                     typename std::enable_if<N == sizeof...(Args), int>::type = 0
                    >
            void set(const std::tuple<Args...> &)
            { }

            /*
             * Get a single key.
             */
            template<typename T,
                     typename std::enable_if<!is_tuple<T>::value, int>::type = 0
                    >
            T get()
            { return Serializer<typename std::decay<T>::type>::to(*this); }

            /*
             * Get multiple keys as tuple.
             * Use for the function parameters.
             */
            template<typename Tuple,
                     typename std::enable_if<is_tuple<Tuple>::value, int>::type = 0
                    >
            Tuple get()
            { return this->get<Tuple>(make_index_sequence<std::tuple_size<Tuple>::value>{}); }

        private:

            template<typename Tuple, std::size_t... I>
            Tuple get(index_sequence<I...>)
            {
                // Initializer lists are always evaluated from left to right
                return {
                    Serializer<typename std::decay<
                        typename std::tuple_element<I, Tuple>::type>
                    ::type>::to(*this)...
                };
            }

    };

    template<> std::uint32_t Message::get_key<std::uint32_t>(int index) const
    {
        return _wisc.read->get_word(std::to_string(index));
    }

    template<> std::vector<std::uint32_t>
    Message::get_key<std::vector<std::uint32_t>>(int index) const
    {
        return _wisc.read->get_word_array(std::to_string(index));
    }

    template<> std::string Message::get_key<std::string>(int index) const
    {
        return _wisc.read->get_string(std::to_string(index));
    }

    template<> std::vector<std::string>
    Message::get_key<std::vector<std::string>>(int index) const
    {
        return _wisc.read->get_string_array(std::to_string(index));
    }

    void Message::set_key(int index, std::uint32_t value)
    {
        _wisc.write->set_word(std::to_string(index), value);
    }

    void Message::set_key(int index, const std::vector<std::uint32_t> &value)
    {
        _wisc.write->set_word_array(std::to_string(index), value);
    }

    void Message::set_key(int index, const std::string &value)
    {
        _wisc.write->set_string(std::to_string(index), value);
    }

    void Message::set_key(int index, const std::vector<std::string> &value)
    {
        _wisc.write->set_string_array(std::to_string(index), value);
    }

    class Connection : public wisc::RPCSvc
    {
    public:
        template<typename Method,
                typename... Args,
                typename std::enable_if<std::is_base_of<RPC::Method, Method>::value, int>::type = 0
                >
        functor_return_t<Method> call(Args&&... args);
    };

    namespace helpers {

        /*
        * Returns the error message describing an exception.
        */
        template<typename Exception>
        std::string get_exception_message(const Exception &e) = delete;

        template<>
        std::string get_exception_message<std::exception>(const std::exception &e)
        {
            return e.what();
        }

        // Never called because the constructor calls abort(). Present in case the
        // issue is corrected in the future.
        template<>
        std::string get_exception_message<wisc::RPCMsg::BadKeyException>(
            const wisc::RPCMsg::BadKeyException &e)
        {
            return "bad RPC key " + e.key;
        }

        template<>
        std::string get_exception_message<wisc::RPCMsg::TypeException>(
            const wisc::RPCMsg::TypeException &e)
        {
            return "RPC type error";
        }

        template<>
        std::string get_exception_message<wisc::RPCMsg::BufferTooSmallException>(
            const wisc::RPCMsg::BufferTooSmallException &e)
        {
            return "RPC buffer too small";
        }

        template<>
        std::string get_exception_message<wisc::RPCMsg::CorruptMessageException>(
            const wisc::RPCMsg::CorruptMessageException &e)
        {
            return "corrupt RPC message: " + e.reason;
        }

        /*
         * Sets the backtrace for the current exception in response
         */
        void set_backtrace(wisc::RPCMsg *response) noexcept
        {
            try
            {
                // Max 30 functions
                std::vector<void *> trace(30);
                int size = backtrace(&trace.front(), 30);

                // Fetch data
                std::unique_ptr<char *> symbols;
                symbols.reset(backtrace_symbols(&trace.front(), size));
                if (symbols == nullptr) {
                    return; // Never fail
                }

                // Send to caller
                std::vector<std::string> bt(symbols.get(), symbols.get() + size);
                response->set_string_array("backtrace", bt);
            }
            catch (...)
            {
//                 LOGGER->log_message(
//                     LogManager::ALERT,
//                     "Could not deduce backtrace. Is memory full?");
            }
        }

        /*
         * Handles an exception, setting the error key on the response.
         *
         * In case an exception occurs when setting the error key,
         * std::terminate is called.
         */
        template<typename Exception>
        void handle_exception(const Exception &e, wisc::RPCMsg *response) noexcept
        {
//          LOGGER->log_message(
//             LogManager::ERROR,
//             "Caught exception: " + get_exception_message(e));
            response->set_string("error", get_exception_message(e));
            set_backtrace(response);
        }

        /*
         * Handles an unknown exception, setting the error key on the response.
         *
         * In case an exception occurs when setting the error key,
         * std::terminate is called.
         */
        void handle_exception(wisc::RPCMsg *response) noexcept
        {
//          LOGGER->log_message(LogManager::ERROR, "Caught unknown exception");
            response->set_string("error", "caught unknown exception");
            set_backtrace(response);
        }

    } // namespace helpers

    /*
     * Thrown by call when the remote call fails.
     */
    class RemoteException : public std::runtime_error
    {
        bool _has_backtrace = false;
        std::vector<std::string> _backtrace;

    public:
        /*
         * Constructor.
         */
        explicit RemoteException(const wisc::RPCMsg &response):
            std::runtime_error("remote error: " + response.get_string("error"))
        {
            _has_backtrace = response.get_key_exists("backtrace");
            if (_has_backtrace) {
                _backtrace = response.get_string_array("backtrace");
            }
        }

        /*
         * Returns true if a backtrace is available.
         */
        bool has_backtrace() const { return _has_backtrace; }

        /*
         * Returns the backtrace if available, the empty string otherwise.
         */
        std::vector<std::string> backtrace() const { return _backtrace; }
    };

    /*
     * Remotely call a RPC method
     */
    template<typename Method,
             typename... Args,
             typename std::enable_if<std::is_base_of<RPC::Method, Method>::value, int>::type = 0
            >
    functor_return_t<Method> Connection::call(Args&&... args)
    {
        wisc::RPCMsg request(std::string(Method::module) + "." + typeid(Method).name());
        Message query{&request};
        query.set(functor_args_t<Method>(std::forward<Args>(args)...));

        const wisc::RPCMsg response = call_method(request);

        if (response.get_key_exists("error")) {
            throw RemoteException(response);
        }

        Message reply{&response};
        return reply.get<functor_return_t<Method>>();
    }

    /*
     * Locally invoke a RPC method
     */
    template<typename Method,
             typename std::enable_if<std::is_base_of<RPC::Method, Method>::value, int>::type = 0
            >
    void invoke(const wisc::RPCMsg *request, wisc::RPCMsg *response) noexcept
    {
        try
        {
            Message query(request);
            auto args = query.get<functor_decay_args_t<Method>>();

            Message reply(response);
            auto result = tuple_apply<functor_return_t<Method>>(Method{}, args);
            reply.set(result);
        }
        catch(const std::exception &e)
        {
            helpers::handle_exception(e, response);
        }
        catch(const wisc::RPCMsg::BadKeyException &e)
        {
            helpers::handle_exception(e, response);
        }
        catch(const wisc::RPCMsg::TypeException &e)
        {
            helpers::handle_exception(e, response);
        }
        catch(const wisc::RPCMsg::BufferTooSmallException &e)
        {
            helpers::handle_exception(e, response);
        }
        catch(const wisc::RPCMsg::CorruptMessageException &e)
        {
            helpers::handle_exception(e, response);
        }
        catch(...)
        {
            helpers::handle_exception(response);
        }
    }

    /*
     * Last but not least, we specialize the serializer for the supported types.
     * Must come after the class Message since it they need the full declaration of the class.
     */
    template<> struct Serializer<uint32_t>
    {
        static void from(Message &msg, const uint32_t value)
        {
            msg.set_key(msg._key_idx++, value);
        }
        static uint32_t to(Message &msg)
        {
            return msg.get_key<std::uint32_t>(msg._key_idx++);
        }
    };

    template<> struct Serializer<std::string>
    {
        static void from(Message &msg, const std::string &value)
        {
            msg.set_key(msg._key_idx++, value);
        }
        static std::string to(Message &msg)
        {
            return msg.get_key<std::string>(msg._key_idx++);
        }
    };

    template<> struct Serializer<std::vector<uint32_t>>
    {
        static void from(Message &msg, const std::vector<uint32_t> &value)
        {
            msg.set_key(msg._key_idx++, value);
        }
        static std::vector<uint32_t> to(Message &msg)
        {
            return msg.get_key<std::vector<std::uint32_t>>(msg._key_idx++);
        }
    };

    template<> struct Serializer<std::vector<std::string>>
    {
        static void from(Message &msg, const std::vector<std::string> &value)
        {
            msg.set_key(msg._key_idx++, value);
        }
        static std::vector<std::string> to(Message &msg)
        {
            return msg.get_key<std::vector<std::string>>(msg._key_idx++);
        }
    };

} // namespace RPC

#endif
