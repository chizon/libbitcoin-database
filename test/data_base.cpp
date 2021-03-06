/**
 * Copyright (c) 2011-2015 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * libbitcoin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License with
 * additional permissions to the one published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version. For more information see LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>
#include <bitcoin/database.hpp>

using namespace boost::system;
using namespace boost::filesystem;
using namespace bc;
using namespace bc::database;
using namespace bc::wallet;

void test_block_exists(const data_base& interface,
    const size_t height, const chain::block block0)
{
    const hash_digest blk_hash = block0.header.hash();
    auto r0 = interface.blocks.get(height);
    auto r0_byhash = interface.blocks.get(blk_hash);

    BOOST_REQUIRE(r0);
    BOOST_REQUIRE(r0_byhash);
    BOOST_REQUIRE(r0.header().hash() == blk_hash);
    BOOST_REQUIRE(r0_byhash.header().hash() == blk_hash);
    BOOST_REQUIRE(r0.height() == height);
    BOOST_REQUIRE(r0_byhash.height() == height);
    BOOST_REQUIRE(r0.transaction_count() == block0.transactions.size());
    BOOST_REQUIRE(r0_byhash.transaction_count() == block0.transactions.size());

    for (size_t i = 0; i < block0.transactions.size(); ++i)
    {
        const chain::transaction& tx = block0.transactions[i];
        const hash_digest tx_hash = tx.hash();
        BOOST_REQUIRE(r0.transaction_hash(i) == tx_hash);
        BOOST_REQUIRE(r0_byhash.transaction_hash(i) == tx_hash);
        auto r0_tx = interface.transactions.get(tx_hash);
        BOOST_REQUIRE(r0_tx);
        BOOST_REQUIRE(r0_byhash);
        BOOST_REQUIRE(r0_tx.transaction().hash() == tx_hash);
        BOOST_REQUIRE(r0_tx.height() == height);
        BOOST_REQUIRE(r0_tx.index() == i);

        if (!tx.is_coinbase())
        {
            for (size_t j = 0; j < tx.inputs.size(); ++j)
            {
                const auto& input = tx.inputs[j];
                chain::input_point spend{ tx_hash, static_cast<uint32_t>(j) };
                auto r0_spend = interface.spends.get(input.previous_output);
                BOOST_REQUIRE(r0_spend.valid);
                BOOST_REQUIRE(r0_spend.hash == spend.hash);
                BOOST_REQUIRE(r0_spend.index == spend.index);

                const auto address = payment_address::extract(input.script);
                if (!address)
                    continue;

                auto history = interface.history.get(address.hash(), 0, 0);
                auto found = false;

                for (const auto row : history)
                {
                    if (row.point.hash == spend.hash &&
                        row.point.index == spend.index)
                    {
                        BOOST_REQUIRE(row.height == height);
                        found = true;
                        break;
                    }
                }

                BOOST_REQUIRE(found);
            }
        }

        for (size_t j = 0; j < tx.outputs.size(); ++j)
        {
            const auto& output = tx.outputs[j];
            chain::output_point outpoint{ tx_hash, static_cast<uint32_t>(j) };
            const auto address = payment_address::extract(output.script);
            if (!address)
                continue;

            auto history = interface.history.get(address.hash(), 0, 0);
            auto found = false;

            for (const auto& row : history)
            {
                bool is_valid = row.point.is_valid();

                BOOST_REQUIRE(is_valid);

                if (row.point.hash == outpoint.hash &&
                    row.point.index == outpoint.index)
                {
                    BOOST_REQUIRE(row.height == height);
                    BOOST_REQUIRE(row.value == output.value);
                    found = true;
                    break;
                }
            }

            BOOST_REQUIRE(found);
        }
    }
}

void test_block_not_exists(const data_base& interface,
    const chain::block block0)
{
    ////const hash_digest blk_hash = hash_block_header(block0.header);
    ////auto r0_byhash = interface.blocks.get(blk_hash);
    ////BOOST_REQUIRE(!r0_byhash);
    for (size_t i = 0; i < block0.transactions.size(); ++i)
    {
        const chain::transaction& tx = block0.transactions[i];
        const hash_digest tx_hash = tx.hash();

        if (!tx.is_coinbase())
        {
            for (size_t j = 0; j < tx.inputs.size(); ++j)
            {
                const auto& input = tx.inputs[j];
                chain::input_point spend{ tx_hash, static_cast<uint32_t>(j) };
                auto r0_spend = interface.spends.get(input.previous_output);
                BOOST_REQUIRE(!r0_spend.valid);

                const auto address = payment_address::extract(input.script);
                if (!address)
                    continue;

                auto history = interface.history.get(address.hash(), 0, 0);
                auto found = false;

                for (const auto& row : history)
                {
                    if (row.point.hash == spend.hash &&
                        row.point.index == spend.index)
                    {
                        found = true;
                        break;
                    }
                }

                BOOST_REQUIRE(!found);
            }
        }

        for (size_t j = 0; j < tx.outputs.size(); ++j)
        {
            const auto& output = tx.outputs[j];
            chain::output_point outpoint{ tx_hash, static_cast<uint32_t>(j) };
            const auto address = payment_address::extract(output.script);
            if (!address)
                continue;

            auto history = interface.history.get(address.hash(), 0, 0);
            auto found = false;

            for (const auto& row : history)
            {
                if (row.point.hash == outpoint.hash &&
                    row.point.index == outpoint.index)
                {
                    found = true;
                    break;
                }
            }

            BOOST_REQUIRE(!found);
        }
    }
}

chain::block read_block(const std::string hex)
{
    data_chunk data;
    BOOST_REQUIRE(decode_base16(data, hex));
    chain::block result;
    BOOST_REQUIRE(result.from_data(data));
    return result;
}

void compare_blocks(const chain::block& popped, const chain::block& original)
{
    BOOST_REQUIRE(popped.header.hash() == original.header.hash());
    BOOST_REQUIRE(popped.transactions.size() == original.transactions.size());

    for (size_t i = 0; i < popped.transactions.size(); ++i)
    {
        BOOST_REQUIRE(popped.transactions[i].hash() ==
            original.transactions[i].hash());
    }
}

#define DIRECTORY "data_base"

class data_base_directory_and_thread_priority_setup_fixture
{
public:
    data_base_directory_and_thread_priority_setup_fixture()
    {
        error_code ec;
        remove_all(DIRECTORY, ec);
        BOOST_REQUIRE(create_directories(DIRECTORY, ec));
        set_thread_priority(thread_priority::lowest);
    }

    ////~data_base_directory_and_thread_priority_setup_fixture()
    ////{
    ////    error_code ec;
    ////    remove_all(DIRECTORY, ec);
    ////    set_thread_priority(thread_priority::normal);
    ////}
};

BOOST_FIXTURE_TEST_SUITE(data_base_tests, data_base_directory_and_thread_priority_setup_fixture)

// TODO: parameterize bucket sizes to control test cost.
BOOST_AUTO_TEST_CASE(data_base__pushpop__test)
{
    std::cout << "begin data_base pushpop test" << std::endl;

    database::settings settings;
    settings.directory = { DIRECTORY };
    settings.stealth_start_height = 0;

    const auto block0 = chain::block::genesis_mainnet();
    boost::filesystem::create_directory(settings.directory);
    BOOST_REQUIRE(data_base::initialize(settings.directory, block0));

    data_base instance(settings);
    BOOST_REQUIRE(instance.start());

    size_t height = 42;
    BOOST_REQUIRE(instance.blocks.top(height));
    BOOST_REQUIRE_EQUAL(height, 0);

    test_block_exists(instance, 0, block0);

    std::cout << "pushpop: block 179" << std::endl;

    // Block #179
    chain::block block1 = read_block(
        "01000000f2c8a8d2af43a9cd05142654e56f41d159ce0274d9cabe15a20eefb5"
        "00000000366c2a0915f05db4b450c050ce7165acd55f823fee51430a8c993e0b"
        "dbb192ede5dc6a49ffff001d192d3f2f02010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff0704ffff001d"
        "0128ffffffff0100f2052a0100000043410435f0d8366085f73906a483097281"
        "55532f24293ea59fe0b33a245c4b8d75f82c3e70804457b7f49322aa822196a7"
        "521e4931f809d7e489bccb4ff14758d170e5ac000000000100000001169e1e83"
        "e930853391bc6f35f605c6754cfead57cf8387639d3b4096c54f18f401000000"
        "48473044022027542a94d6646c51240f23a76d33088d3dd8815b25e9ea18cac6"
        "7d1171a3212e02203baf203c6e7b80ebd3e588628466ea28be572fe1aaa3f309"
        "47da4763dd3b3d2b01ffffffff0200ca9a3b00000000434104b5abd412d4341b"
        "45056d3e376cd446eca43fa871b51961330deebd84423e740daa520690e1d9e0"
        "74654c59ff87b408db903649623e86f1ca5412786f61ade2bfac005ed0b20000"
        "000043410411db93e1dcdb8a016b49840f8c53bc1eb68a382e97b1482ecad7b1"
        "48a6909a5cb2e0eaddfb84ccf9744464f82e160bfa9b8b64f9d4c03f999b8643"
        "f656b412a3ac00000000");
    test_block_not_exists(instance, block1);
    instance.push(block1);
    test_block_exists(instance, 1, block1);

    BOOST_REQUIRE(instance.blocks.top(height));
    BOOST_REQUIRE_EQUAL(height, 1u);

    std::cout << "pushpop: block 181" << std::endl;

    // Block #181
    chain::block block2 = read_block(
        "01000000e5c6af65c46bd826723a83c1c29d9efa189320458dc5298a0c8655dc"
        "0000000030c2a0d34bfb4a10d35e8166e0f5a37bce02fc1b85ff983739a19119"
        "7f010f2f40df6a49ffff001d2ce7ac9e02010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff0704ffff001d"
        "0129ffffffff0100f2052a01000000434104b10dd882c04204481116bd4b4151"
        "0e98c05a869af51376807341fc7e3892c9034835954782295784bfc763d9736e"
        "d4122c8bb13d6e02c0882cb7502ce1ae8287ac000000000100000001be141eb4"
        "42fbc446218b708f40caeb7507affe8acff58ed992eb5ddde43c6fa101000000"
        "4847304402201f27e51caeb9a0988a1e50799ff0af94a3902403c3ad4068b063"
        "e7b4d1b0a76702206713f69bd344058b0dee55a9798759092d0916dbbc3e592f"
        "ee43060005ddc17401ffffffff0200e1f5050000000043410401518fa1d1e1e3"
        "e162852d68d9be1c0abad5e3d6297ec95f1f91b909dc1afe616d6876f9291845"
        "1ca387c4387609ae1a895007096195a824baf9c38ea98c09c3ac007ddaac0000"
        "000043410411db93e1dcdb8a016b49840f8c53bc1eb68a382e97b1482ecad7b1"
        "48a6909a5cb2e0eaddfb84ccf9744464f82e160bfa9b8b64f9d4c03f999b8643"
        "f656b412a3ac00000000");
    test_block_not_exists(instance, block2);
    instance.push(block2);
    test_block_exists(instance, 2, block2);

    BOOST_REQUIRE(instance.blocks.top(height));
    BOOST_REQUIRE_EQUAL(height, 2u);

    std::cout << "pushpop: block 183" << std::endl;

    // Block #183
    chain::block block3 = read_block(
        "01000000bed482ccb42bf5c20d00a5bb9f7d688e97b94c622a7f42f3aaf23f8b"
        "000000001cafcb3e4cad2b4eed7fb7fcb7e49887d740d66082eb45981194c532"
        "b58d475258ee6a49ffff001d1bc0e23202010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff0704ffff001d"
        "011affffffff0100f2052a0100000043410435d66d6cef63a3461110c810975b"
        "8816308372b58274d88436a974b478d98d8d972f7233ea8a5242d151de9d4b1a"
        "c11a6f7f8460e8f9b146d97c7bad980cc5ceac000000000100000001ba91c1d5"
        "e55a9e2fab4e41f55b862a73b24719aad13a527d169c1fad3b63b51200000000"
        "48473044022041d56d649e3ca8a06ffc10dbc6ba37cb958d1177cc8a155e83d0"
        "646cd5852634022047fd6a02e26b00de9f60fb61326856e66d7a0d5e2bc9d01f"
        "b95f689fc705c04b01ffffffff0100e1f50500000000434104fe1b9ccf732e1f"
        "6b760c5ed3152388eeeadd4a073e621f741eb157e6a62e3547c8e939abbd6a51"
        "3bf3a1fbe28f9ea85a4e64c526702435d726f7ff14da40bae4ac00000000");
    test_block_not_exists(instance, block3);
    instance.push(block3);
    test_block_exists(instance, 3, block3);

    std::cout << "pushpop: cleanup tests" << std::endl;

    BOOST_REQUIRE(instance.blocks.top(height));
    BOOST_REQUIRE_EQUAL(height, 3u);

    chain::block block3_popped = instance.pop();
    BOOST_REQUIRE(instance.blocks.top(height));
    BOOST_REQUIRE_EQUAL(height, 2u);
    compare_blocks(block3_popped, block3);

    test_block_not_exists(instance, block3);
    test_block_exists(instance, 2, block2);
    test_block_exists(instance, 1, block1);
    test_block_exists(instance, 0, block0);

    chain::block block2_popped = instance.pop();
    BOOST_REQUIRE(instance.blocks.top(height));
    BOOST_REQUIRE_EQUAL(height, 1u);
    compare_blocks(block2_popped, block2);

    test_block_not_exists(instance, block3);
    test_block_not_exists(instance, block2);
    test_block_exists(instance, 1, block1);
    test_block_exists(instance, 0, block0);

    std::cout << "end pushpop test" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
