/*
 * Copyright (C) 2002, 2011 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 */

/*
 * The contents of this file are subject to the terms of the Common
 * Development and Distribution License (the "License") version 1.0
 * and no later version.  You may not use this file except in
 * compliance with the License.
 *
 * You can obtain a copy of the License at
 *         http://www.opensource.org/licenses/cddl1.php
 *
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 */

/*
 * vmware_pack_begin.h --
 *
 *    Begin of structure packing. See vmware_pack_init.h for details.
 *
 *    Note that we do not use the following construct in this include file,
 *    because we want to emit the code every time the file is included --hpreg
 *
 *    #ifndef foo
 *    #   define foo
 *    ...
 *    #endif
 *
 */

#ifndef	_VMWARE_PACK_BEGIN_H_
#define	_VMWARE_PACK_BEGIN_H_

#ifndef __GNUC__
#pragma pack(1)
#endif /* __GNUC__ */

#endif /* _VMWARE_PACK_BEGIN_H_ */
