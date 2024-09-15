/**
 * @author Timo Meyer (timoxd7@gmx.de)
 * @brief This is a modified Copy of the Callback from Steroido Repository
 * (https://github.com/timoxd7/Steroido/)
 *
 * @copyright Copyright (c) 2024 Timo Meyer
 *
 */

#pragma once

#include <stdint.h>

#include <new>
#include <type_traits>

#ifdef USE_SDK_CONFIG
#include "sdkconfig.h"
#endif

#ifdef CONFIG_CALLBACK_INCLUDE_POINT_TO_SAME
#include <typeinfo>
#endif

#if defined(PC_BUILD) && (__linux__ || __LP64__ || __APPLE__ || __MACH__)
#define CALLBACK_INTERNAL_BUFFER_SIZE 32   // Byte
#else
#define CALLBACK_INTERNAL_BUFFER_SIZE 16   // Byte
#endif

/**
 * @brief Simple Interface to compare Callbacks of unknown Type
 *
 */
class CallbackCompare {
   public:
#ifdef CONFIG_CALLBACK_INCLUDE_POINT_TO_SAME
    virtual bool pointToSame(const CallbackCompare &other) const = 0;
#endif

    virtual bool isCallbackSet() const = 0;
};

/**
 * @brief A generic class to point to a specific Function or Method
 *
 * NOTE: Will never change its pointing to callback. The callback can only point to something at
 * construction and deleted or killed afterwards, but the callback can not change.
 *
 * @tparam R Return type
 * @tparam ArgTs Optional Arguments
 */
template <typename R, typename... ArgTs>
class Callback : public CallbackCompare {
   public:
    /**
     * @brief Creates an empty callback with no destination
     *
     */
    constexpr Callback() : _callbackSet(false) {}

    /**
     * @brief Construct a Callback using a Function (-pointer)
     *
     * @param func Function to be called on call()
     */
    Callback(R (*const func)(ArgTs... args)) : _callbackSet(true) {
        _checkSizeFit<FunctionCaller, CALLBACK_INTERNAL_BUFFER_SIZE>();

        // Special Case: Check for nullptr
        if (func == nullptr) {
            _callbackSet = false;
            return;
        }

        // Construct the FunctionCaller in the internal buffer
        new (_buffer) FunctionCaller(func);
    }

    /**
     * @brief Construct a Callback using a method of an Instance of an Class
     *
     * @tparam T
     * @param obj The Instance of the Object the Method should be called on
     * @param method The Method (-pointer) to the method of the Class which should be called
     */
    template <typename T>
    Callback(T *const obj, R (T::*const method)(ArgTs... args)) : _callbackSet(true) {
        _checkSizeEqual<MethodCaller<T>, CALLBACK_INTERNAL_BUFFER_SIZE>();

        // Special Case: Check for nullptr (normally only interesting for fuction, but whatever)
        if (obj == nullptr || method == nullptr) {
            _callbackSet = false;
            return;
        }

        // Construct the MethodCaller in the internal buffer
        new (_buffer) MethodCaller<T>(obj, method);
    }

    /**
     * @brief Call the Callback
     *
     * @return R
     */
    R call(ArgTs... args) const { return _call<R, ArgTs...>(args...); }

    /**
     * @brief Shorthand for call()
     *
     * @param args
     * @return R
     */
    inline R operator()(ArgTs... args) const { return call(args...); }

    /**
     * @brief Check if the callback was set
     *
     * @return true
     * @return false
     */
    inline bool isCallbackSet() const override { return _callbackSet; }

#ifdef CONFIG_CALLBACK_INCLUDE_POINT_TO_SAME
    /**
     * @brief Check if this unique Callback is pointing to the same destination as another unique
     * Callback.
     *
     * @param other
     * @return true
     * @return false
     */
    bool pointToSame(const Callback<R, ArgTs...> &other) const {
        if (!_callbackSet) {
            if (!other._callbackSet) {
                return true;
            }
        } else {
            // -> Check if the caller behind is the same
            if (other._callbackSet) {
                return ((const InternalCallable *)_buffer)
                    ->tryCompare(((const InternalCallable *)other._buffer));
            }
        }

        return false;
    }

    bool pointToSame(const CallbackCompare &otherCallable) const override {
        // Check if the Callback Type is exactly the same
        if (typeid(otherCallable) == typeid(*this)) {
            return pointToSame((Callback<R, ArgTs...> &)otherCallable);
        }

        return false;
    }

    /**
     * @brief Shorthand for pointToSame()
     *
     * @param other
     * @return true
     * @return false
     */
    inline bool operator==(const Callback<R, ArgTs...> &other) const { return pointToSame(other); }
    inline bool operator==(const CallbackCompare &otherCallable) const {
        return pointToSame(otherCallable);
    }
#endif

   private:
    class InternalCallable;
    class FunctionCaller;

    template <typename T>
    class MethodCaller;

    // Member Variables
    bool _callbackSet;
    uint8_t _buffer[CALLBACK_INTERNAL_BUFFER_SIZE]{};

    template <typename RN, typename... ArgTsN>
    inline typename std::enable_if<!std::is_same<RN, void>::value, RN>::type _call(
        ArgTsN... args) const {
        if (_callbackSet) {
            return ((InternalCallable *)_buffer)->call(args...);
        }

        return (RN)0;
    }

    template <typename RN, typename... ArgTsN>
    inline typename std::enable_if<std::is_same<RN, void>::value, RN>::type _call(
        ArgTsN... args) const {
        if (_callbackSet) {
            ((InternalCallable *)_buffer)->call(args...);
        }
    }

    template <typename ToCheck, std::size_t ExpectedSize, std::size_t RealSize = sizeof(ToCheck)>
    constexpr void _checkSizeEqual() {
        static_assert(ExpectedSize >= RealSize, "Internal Buffer is too small!");
        static_assert(ExpectedSize == RealSize, "Internal Buffer is too big!");
    }

    template <typename ToCheck, std::size_t MaxSize, std::size_t RealSize = sizeof(ToCheck)>
    constexpr void _checkSizeFit() {
        static_assert(MaxSize >= RealSize, "Internal Buffer is too small!");
    }

    /**
     * @brief InternalCallable Interface
     *
     * @tparam R Return Value of the assigned callback function
     */

    class InternalCallable {
       public:
        virtual R call(ArgTs... args) const = 0;

#ifdef CONFIG_CALLBACK_INCLUDE_POINT_TO_SAME
        /**
         * @brief Use this function with caution! Only FunctionCaller can be compared to each other
         * and only MethodCaller can be compared. Cross-Type comparrisson is not allowed ->
         * undefined. behaviour
         *
         * @return callable_type_t
         */
        virtual bool tryCompare(const InternalCallable *other) const = 0;
#endif
    };

    /**
     * @brief Specific caller using the InternalCallable Interface
     *
     */
    class FunctionCaller : public InternalCallable {
       public:
        constexpr FunctionCaller(R (*const func)(ArgTs...)) : _func(func) {}

        R call(ArgTs... args) const { return (*_func)(args...); }

#ifdef CONFIG_CALLBACK_INCLUDE_POINT_TO_SAME
        bool tryCompare(const InternalCallable *otherPtr) const {
            if (otherPtr != nullptr) {
                if (typeid(*otherPtr) == typeid(*this)) {
                    FunctionCaller *otherFunc = (FunctionCaller *)otherPtr;

                    if (otherFunc->_func == _func) {
                        return true;
                    }
                }
            }

            return false;
        }
#endif

       private:
        constexpr FunctionCaller() {}

        R (*const _func)(ArgTs...);
    };

    template <typename T>
    class MethodCaller : public InternalCallable {
       public:
        constexpr MethodCaller(T *const obj, R (T::*const method)(ArgTs...))
            : _obj(obj), _method(method) {}

        R call(ArgTs... args) const { return (*_obj.*_method)(args...); }

#ifdef CONFIG_CALLBACK_INCLUDE_POINT_TO_SAME
        bool tryCompare(const InternalCallable *otherCallable) const {
            if (otherCallable != nullptr) {
                if (typeid(*otherCallable) == typeid(*this)) {
                    MethodCaller<T> *otherPtr = (MethodCaller<T> *)otherCallable;

                    if (otherPtr->_obj == _obj) {
                        if (otherPtr->_method == _method) {
                            return true;
                        }
                    }
                }
            }

            return false;
        }
#endif

       private:
        constexpr MethodCaller() {}

        T *const _obj;
        R (T::*const _method)(ArgTs...);
    };
};

// -------------- Functions for easier and faster access to a Callback

/**
 * @brief Callback for a single function
 *
 * @tparam R
 * @param func
 * @return Callback<R>
 */
template <typename R, typename... ArgTs>
Callback<R, ArgTs...> callback(R (*func)(ArgTs...)) {
    return Callback<R, ArgTs...>(func);
}

/**
 * @brief Callback for an Object's function
 *
 * @tparam T
 * @tparam R
 * @param obj
 * @param method
 * @return Callback<R>
 */
template <typename T, typename R, typename... ArgTs>
Callback<R, ArgTs...> callback(T *obj, R (T::*method)(ArgTs...)) {
    return Callback<R, ArgTs...>(obj, method);
}

/**
 * @brief Callback for an Object's function
 *
 * @tparam T
 * @tparam R
 * @param obj
 * @param method
 * @return Callback<R>
 */
template <typename T, typename R, typename... ArgTs>
Callback<R, ArgTs...> callback(T &obj, R (T::*method)(ArgTs...)) {
    return Callback<R, ArgTs...>(&obj, method);
}
