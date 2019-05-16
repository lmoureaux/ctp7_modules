#ifndef RPC_HPP
#define RPC_HPP

#include <type_traits>
#include <utility>
#include <tuple>

// Supported types
#include <cstdint>
#include <string>
#include <vector>

// Remove once backend is implemented
#include <iostream>

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

        protected:
            /*
             * Index of the next free/unread key.
             */
            uint32_t _key_idx = 0;

            // The serializer is a friend so that it has access to the key index
            // Could write a "key dispenser method" ?
            template<typename T> friend struct Serializer;

        public:
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

    /*
     * Remotely call a RPC method
     */
    template<typename Method,
             typename... Args,
             typename std::enable_if<std::is_base_of<RPC::Method, Method>::value, int>::type = 0
            >
    functor_return_t<Method> call(Args&&... args)
    {
        std::cout << "Will call : " << Method::name << " (typeid name : " << typeid(Method).name() << " )" << std::endl;

        std::cout << "Writing arguments..." << std::endl;
        Message query{};
        query.set(functor_args_t<Method>(std::forward<Args>(args)...));

        std::cout << "Checking for error key in response..." << std::endl;

        std::cout << "Fetching response..." << std::endl;
        Message reply{};
        return reply.get<functor_return_t<Method>>();
    }

    /*
     * Locally invoke a RPC method
     */
    template<typename Method,
             typename std::enable_if<std::is_base_of<RPC::Method, Method>::value, int>::type = 0
            >
    void invoke(Message &&query, Message &&reply) noexcept
    {
        try
        {
            std::cout << "Fetching arguments..." << std::endl;
            auto args = query.get<functor_decay_args_t<Method>>();

            std::cout << "Writing response..." << std::endl;

            auto result = tuple_apply<functor_return_t<Method>>(Method{}, args);
            reply.set(result);
        }
        catch (const std::exception &e)
        {
            try
            {
                std::cout << "Exception caught! Set key (error) to " << e.what() << std::endl;
            }
            catch (const std::exception &e)
            {
                // If an exception occurs while we are trying to return the exception to the caller,
                // the best we can do is logging the exception message and terminate the server.
                // The client will see the client disconnected.
                std::cout << "Cannot send back the error message to the caller." << std::endl;
                std::cout << "The error message is : " << e.what() << std::endl;
                std::cout << "Terminating the server..." << std::endl;
                std::exit(1);
            }
        }
    }

    /*
     * Last but not least, we specialize the serializer for the supported types.
     * Must come after the class Message since it they need the full declaration of the class.
     */
    template<> struct Serializer<uint32_t>
    {
        static void from(Message &Msg, const uint32_t value)
        { std::cout << "Setting key (" << Msg._key_idx++ << ") to uint32_t : " << value << std::endl; }
        static uint32_t to(Message &Msg)
        {
            std::cout << "Fetching key (" << Msg._key_idx++ << ") as uint32_t" << std::endl;
            return uint32_t{};
        }
    };

    template<> struct Serializer<std::string>
    {
        static void from(Message &Msg, const std::string &value)
        { std::cout << "Setting key (" << Msg._key_idx++ << ") to std::string : " << value << std::endl; }
        static std::string to(Message &Msg)
        {
            std::cout << "Fetching key (" << Msg._key_idx++ << ") as std::string" << std::endl;
            return std::string{"testArgument"};
        }
    };

    template<> struct Serializer<std::vector<uint32_t>>
    {
        static void from(Message &Msg, const std::vector<uint32_t> &)
        { std::cout << "Setting key (" << Msg._key_idx++ << ") to std::vector<uint32_t>" << std::endl; }
        static std::vector<uint32_t> to(Message &Msg)
        {
            std::cout << "Fetching key (" << Msg._key_idx++ << ") as std::vector<uint32_t>" << std::endl;
            return std::vector<uint32_t>{};
        }
    };

    template<> struct Serializer<std::vector<std::string>>
    {
        static void from(Message &Msg, const std::vector<std::string> &)
        { std::cout << "Setting key (" << Msg._key_idx++ << ") to std::vector<std::string>" << std::endl; }
        static std::vector<std::string> to(Message &Msg)
        {
            std::cout << "Fetching key (" << Msg._key_idx++ << ") as std::vector<std::string>" << std::endl;
            return std::vector<std::string>{};
        }
    };

} // namespace RPC

#endif
