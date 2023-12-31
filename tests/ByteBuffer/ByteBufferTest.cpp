
// ByteBufferTest.cpp

// Implements the main app entrypoint for the cByteBuffer class test

#include "Globals.h"
#include "../TestHelpers.h"
#include "ByteBuffer.h"





static void TestRead(void)
{
	cByteBuffer buf(50);
	buf.Write("\x05\xac\x02\x00", 4);
	UInt32 v1;
	TEST_TRUE(buf.ReadVarInt(v1));
	TEST_EQUAL(v1, 5);
	UInt32 v2;
	TEST_TRUE(buf.ReadVarInt(v2));
	TEST_EQUAL(v2, 300);
	UInt32 v3;
	TEST_TRUE(buf.ReadVarInt(v3));
	TEST_EQUAL(v3, 0);
}





static void TestWrite(void)
{
	cByteBuffer buf(50);
	buf.WriteVarInt32(5);
	buf.WriteVarInt32(300);
	buf.WriteVarInt32(0);
	ContiguousByteBuffer All;
	buf.ReadAll(All);
	TEST_EQUAL(All.size(), 4);
	TEST_EQUAL(memcmp(All.data(), "\x05\xac\x02\x00", All.size()), 0);
}





static void TestWrap(void)
{
	cByteBuffer buf(3);
	for (int i = 0; i < 1000; i++)
	{
		size_t FreeSpace = buf.GetFreeSpace();
		TEST_EQUAL(buf.GetReadableSpace(), 0);
		TEST_GREATER_THAN_OR_EQUAL(FreeSpace, 1);
		TEST_TRUE(buf.Write("a", 1));
		TEST_TRUE(buf.CanReadBytes(1));
		TEST_EQUAL(buf.GetReadableSpace(), 1);
		UInt8 v = 0;
		TEST_TRUE(buf.ReadBEUInt8(v));
		TEST_EQUAL(v, 'a');
		TEST_EQUAL(buf.GetReadableSpace(), 0);
		buf.CommitRead();
		TEST_EQUAL(buf.GetFreeSpace(), FreeSpace);  // We're back to normal
	}
}





static void TestXYZPositionRoundtrip(void)
{
	cByteBuffer buf(50);
	buf.WriteXYZPosition64(-33554432, -2048, -33554432); // Testing the minimun values
	int x, y, z;
	TEST_TRUE(buf.ReadXYZPosition64(x, y, z));
	TEST_EQUAL(x, -33554432);
	TEST_EQUAL(y, -2048);
	TEST_EQUAL(z, -33554432);
}





static void TestXZYPositionRoundtrip(void)
{
	cByteBuffer buf(50);
	buf.WriteXZYPosition64(-33554432, -2048, -33554432); // Testing the minimun values
	int x, y, z;
	TEST_TRUE(buf.ReadXZYPosition64(x, y, z));
	TEST_EQUAL(x, -33554432);
	TEST_EQUAL(y, -2048);
	TEST_EQUAL(z, -33554432);
}





IMPLEMENT_TEST_MAIN("ByteBuffer",
	TestRead();
	TestWrite();
	TestWrap();
	TestXYZPositionRoundtrip();
	TestXZYPositionRoundtrip();
)
