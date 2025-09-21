#ifndef MINING_NOTIFY_H
#define MINING_NOTIFY_H
typedef struct
{
    char *job_id;
    char *prev_block_hash;
    char *coinbase_1;
    char *coinbase_2;
    uint8_t *merkle_branches;
    size_t n_merkle_branches;
    uint32_t version;
    uint32_t target;
    uint32_t ntime;
    uint32_t job_difficulty;
} mining_notify;

#endif