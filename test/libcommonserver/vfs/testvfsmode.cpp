/*
 * Infomaniak kDrive - Desktop
 * Copyright (C) 2023-2026 Infomaniak Network SA
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "testvfsmode.h"

#include "libcommonserver/vfs/vfs.h"

namespace KDC {

void TestVfsMode::testModeToFromString() {
    // Strings are persisted in the sync configuration and must stay stable.
    CPPUNIT_ASSERT(Vfs::modeToString(VirtualFileMode::Off) == Str("off"));
    CPPUNIT_ASSERT(Vfs::modeToString(VirtualFileMode::Win) == Str("wincfapi"));
    CPPUNIT_ASSERT(Vfs::modeToString(VirtualFileMode::Mac) == Str("mac"));
    CPPUNIT_ASSERT(Vfs::modeToString(VirtualFileMode::Linux) == Str("linux"));

    CPPUNIT_ASSERT(Vfs::modeFromString(QStringLiteral("off")) == VirtualFileMode::Off);
    CPPUNIT_ASSERT(Vfs::modeFromString(QStringLiteral("wincfapi")) == VirtualFileMode::Win);
    CPPUNIT_ASSERT(Vfs::modeFromString(QStringLiteral("mac")) == VirtualFileMode::Mac);
    CPPUNIT_ASSERT(Vfs::modeFromString(QStringLiteral("linux")) == VirtualFileMode::Linux);

    // Unknown values fall back to the default mode.
    CPPUNIT_ASSERT(Vfs::modeFromString(QStringLiteral("unknown")) == VirtualFileMode::Off);
    CPPUNIT_ASSERT(Vfs::modeFromString(QString()) == VirtualFileMode::Off);
}

void TestVfsMode::testModeIntValues() {
    // The integer values are persisted in the sync table: appending is allowed, reordering is not.
    CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(VirtualFileMode::Off));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(VirtualFileMode::Win));
    CPPUNIT_ASSERT_EQUAL(2, static_cast<int>(VirtualFileMode::Mac));
    CPPUNIT_ASSERT_EQUAL(3, static_cast<int>(VirtualFileMode::Linux));
    CPPUNIT_ASSERT_EQUAL(4, static_cast<int>(VirtualFileMode::EnumEnd));
}

} // namespace KDC
