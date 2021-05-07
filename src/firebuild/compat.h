/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_COMPAT_H_
#define FIREBUILD_COMPAT_H_

#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/version.hpp>

/**
 * Workarounds to build Firebuils with older dependencies
 */

#if BOOST_VERSION < 107400

/* Copied from boost 1.74's local_shared_ptr.hpp with minor reformatting */

//  Copyright 2017 Peter Dimov
//
//  Distributed under the Boost Software License, Version 1.0. (See
//  accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

// std::hash
namespace std {

template<class T> struct hash< ::boost::local_shared_ptr<T> > {
    std::size_t operator()(::boost::local_shared_ptr<T> const & p) const BOOST_SP_NOEXCEPT {
        return std::hash< typename ::boost::local_shared_ptr<T>::element_type* >()( p.get() );
    }
};

}  // namespace std

#endif  // BOOST_VERSION < 107400

#endif  // FIREBUILD_COMPAT_H_
