/*****************************************************************************
 *   Test Suite of The MCLinker Project,                                     *
 *                                                                           *
 *   Copyright (C), 2011 -                                                   *
 *   Embedded and Web Computing Lab, National Taiwan University              *
 *   MediaTek, Inc.                                                          *
 *                                                                           *
 *   Duo <pinronglu@gmail.com>                                               *
 ****************************************************************************/
#ifndef MCARCHIVEREADER_TEST_H
#define MCARCHIVEREADER_TEST_H

#include <mcld/MC/MCArchiveReader.h>
#include <gtest.h>

namespace mcld
{
class MCArchiveReader;

} // namespace for mcld

namespace mcldtest
{

/** \class MCArchiveReaderTest
 *  \brief test for MCArchiveReader
 *
 *  \see MCArchiveReader 
 */
class MCArchiveReaderTest : public ::testing::Test
{
public:
	// Constructor can do set-up work for all test here.
	MCArchiveReaderTest();

	// Destructor can do clean-up work that doesn't throw exceptions here.
	virtual ~MCArchiveReaderTest();

	// SetUp() will be called immediately before each test.
	virtual void SetUp();

	// TearDown() will be called immediately after each test.
	virtual void TearDown();

protected:
	mcld::MCArchiveReader* m_pTestee;
};

} // namespace of mcldtest

#endif

