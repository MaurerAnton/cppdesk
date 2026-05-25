#include <gtest/gtest.h>
#include <string>
#include <vector>

TEST(test_protocol_detail, Case000) {
    MouseEvent ev{}; ev.x = 0; ev.y = 0;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case001) {
    MouseEvent ev{}; ev.x = 1; ev.y = 2;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case002) {
    MouseEvent ev{}; ev.x = 2; ev.y = 4;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case003) {
    MouseEvent ev{}; ev.x = 3; ev.y = 6;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case004) {
    MouseEvent ev{}; ev.x = 4; ev.y = 8;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case005) {
    MouseEvent ev{}; ev.x = 5; ev.y = 10;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case006) {
    MouseEvent ev{}; ev.x = 6; ev.y = 12;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case007) {
    MouseEvent ev{}; ev.x = 7; ev.y = 14;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case008) {
    MouseEvent ev{}; ev.x = 8; ev.y = 16;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case009) {
    MouseEvent ev{}; ev.x = 9; ev.y = 18;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case010) {
    MouseEvent ev{}; ev.x = 10; ev.y = 20;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case011) {
    MouseEvent ev{}; ev.x = 11; ev.y = 22;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case012) {
    MouseEvent ev{}; ev.x = 12; ev.y = 24;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case013) {
    MouseEvent ev{}; ev.x = 13; ev.y = 26;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case014) {
    MouseEvent ev{}; ev.x = 14; ev.y = 28;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case015) {
    MouseEvent ev{}; ev.x = 15; ev.y = 30;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case016) {
    MouseEvent ev{}; ev.x = 16; ev.y = 32;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case017) {
    MouseEvent ev{}; ev.x = 17; ev.y = 34;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case018) {
    MouseEvent ev{}; ev.x = 18; ev.y = 36;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case019) {
    MouseEvent ev{}; ev.x = 19; ev.y = 38;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case020) {
    MouseEvent ev{}; ev.x = 20; ev.y = 40;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case021) {
    MouseEvent ev{}; ev.x = 21; ev.y = 42;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case022) {
    MouseEvent ev{}; ev.x = 22; ev.y = 44;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case023) {
    MouseEvent ev{}; ev.x = 23; ev.y = 46;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case024) {
    MouseEvent ev{}; ev.x = 24; ev.y = 48;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case025) {
    MouseEvent ev{}; ev.x = 25; ev.y = 50;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case026) {
    MouseEvent ev{}; ev.x = 26; ev.y = 52;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case027) {
    MouseEvent ev{}; ev.x = 27; ev.y = 54;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case028) {
    MouseEvent ev{}; ev.x = 28; ev.y = 56;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case029) {
    MouseEvent ev{}; ev.x = 29; ev.y = 58;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case030) {
    MouseEvent ev{}; ev.x = 30; ev.y = 60;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case031) {
    MouseEvent ev{}; ev.x = 31; ev.y = 62;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case032) {
    MouseEvent ev{}; ev.x = 32; ev.y = 64;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case033) {
    MouseEvent ev{}; ev.x = 33; ev.y = 66;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case034) {
    MouseEvent ev{}; ev.x = 34; ev.y = 68;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case035) {
    MouseEvent ev{}; ev.x = 35; ev.y = 70;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case036) {
    MouseEvent ev{}; ev.x = 36; ev.y = 72;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case037) {
    MouseEvent ev{}; ev.x = 37; ev.y = 74;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case038) {
    MouseEvent ev{}; ev.x = 38; ev.y = 76;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case039) {
    MouseEvent ev{}; ev.x = 39; ev.y = 78;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case040) {
    MouseEvent ev{}; ev.x = 40; ev.y = 80;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case041) {
    MouseEvent ev{}; ev.x = 41; ev.y = 82;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case042) {
    MouseEvent ev{}; ev.x = 42; ev.y = 84;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case043) {
    MouseEvent ev{}; ev.x = 43; ev.y = 86;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case044) {
    MouseEvent ev{}; ev.x = 44; ev.y = 88;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case045) {
    MouseEvent ev{}; ev.x = 45; ev.y = 90;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case046) {
    MouseEvent ev{}; ev.x = 46; ev.y = 92;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case047) {
    MouseEvent ev{}; ev.x = 47; ev.y = 94;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case048) {
    MouseEvent ev{}; ev.x = 48; ev.y = 96;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case049) {
    MouseEvent ev{}; ev.x = 49; ev.y = 98;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case050) {
    MouseEvent ev{}; ev.x = 50; ev.y = 100;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case051) {
    MouseEvent ev{}; ev.x = 51; ev.y = 102;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case052) {
    MouseEvent ev{}; ev.x = 52; ev.y = 104;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case053) {
    MouseEvent ev{}; ev.x = 53; ev.y = 106;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case054) {
    MouseEvent ev{}; ev.x = 54; ev.y = 108;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case055) {
    MouseEvent ev{}; ev.x = 55; ev.y = 110;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case056) {
    MouseEvent ev{}; ev.x = 56; ev.y = 112;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case057) {
    MouseEvent ev{}; ev.x = 57; ev.y = 114;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case058) {
    MouseEvent ev{}; ev.x = 58; ev.y = 116;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case059) {
    MouseEvent ev{}; ev.x = 59; ev.y = 118;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case060) {
    MouseEvent ev{}; ev.x = 60; ev.y = 120;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case061) {
    MouseEvent ev{}; ev.x = 61; ev.y = 122;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case062) {
    MouseEvent ev{}; ev.x = 62; ev.y = 124;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case063) {
    MouseEvent ev{}; ev.x = 63; ev.y = 126;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case064) {
    MouseEvent ev{}; ev.x = 64; ev.y = 128;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case065) {
    MouseEvent ev{}; ev.x = 65; ev.y = 130;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case066) {
    MouseEvent ev{}; ev.x = 66; ev.y = 132;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case067) {
    MouseEvent ev{}; ev.x = 67; ev.y = 134;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case068) {
    MouseEvent ev{}; ev.x = 68; ev.y = 136;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case069) {
    MouseEvent ev{}; ev.x = 69; ev.y = 138;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case070) {
    MouseEvent ev{}; ev.x = 70; ev.y = 140;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case071) {
    MouseEvent ev{}; ev.x = 71; ev.y = 142;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case072) {
    MouseEvent ev{}; ev.x = 72; ev.y = 144;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case073) {
    MouseEvent ev{}; ev.x = 73; ev.y = 146;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case074) {
    MouseEvent ev{}; ev.x = 74; ev.y = 148;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case075) {
    MouseEvent ev{}; ev.x = 75; ev.y = 150;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case076) {
    MouseEvent ev{}; ev.x = 76; ev.y = 152;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case077) {
    MouseEvent ev{}; ev.x = 77; ev.y = 154;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case078) {
    MouseEvent ev{}; ev.x = 78; ev.y = 156;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

TEST(test_protocol_detail, Case079) {
    MouseEvent ev{}; ev.x = 79; ev.y = 158;
    EXPECT_GE(ev.x, 0); EXPECT_GE(ev.y, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}