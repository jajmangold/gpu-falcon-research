/*
 * Offline harness for the padding-walk logic in nvdrv's vbiosRSADecrypt().
 *
 * This does not talk to RM, PCI, BAR0, VBIOS, or a GPU. It models only the
 * post-RSA-decrypt byte walk over a heap buffer shaped like BIG_NUM.value.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define RSA_PKCS1_PADDING 0x01u

static int modeled_padding_extract(const uint8_t *res_value, uint32_t res_size,
                                   uint8_t **out, uint32_t *out_len)
{
    uint32_t offset;

    if (out == NULL || out_len == NULL)
        return -100;

    *out = NULL;
    *out_len = 0;

    /*
     * This intentionally mirrors the reviewed code's assumptions:
     *
     *   offset = res.size - 1;
     *   check trailing 0x00, then 0x01, then scan 0xff bytes downward
     *   without an explicit offset > 0 guard.
     */
    offset = res_size - 1;

    if (*(res_value + offset) != 0)
        return -1;

    offset--;
    if (*(res_value + offset) != RSA_PKCS1_PADDING)
        return -2;

    offset--;
    while (*(res_value + offset) == 0xff)
        offset--;

    if (*(res_value + offset) != 0)
        return -3;

    *out = malloc(offset == 0 ? 1 : offset);
    if (*out == NULL)
        return -4;

    memcpy(*out, res_value, offset);
    *out_len = offset;
    return 0;
}

static int run_case(const char *name, uint8_t *buf, uint32_t size)
{
    uint8_t *out = NULL;
    uint32_t out_len = 0;
    int rc;

    printf("case=%s size=%u\n", name, size);
    fflush(stdout);

    rc = modeled_padding_extract(buf, size, &out, &out_len);
    printf("case=%s rc=%d out_len=%u\n", name, rc, out_len);
    free(out);
    return rc;
}

static void child_case_valid(void)
{
    uint8_t buf[16];

    memset(buf, 0xaa, sizeof(buf));
    buf[0] = 0x11;
    buf[1] = 0x22;
    buf[2] = 0x33;
    buf[3] = 0x44;
    buf[4] = 0x00;
    memset(buf + 5, 0xff, 9);
    buf[14] = RSA_PKCS1_PADDING;
    buf[15] = 0x00;

    (void)run_case("valid-shaped", buf, sizeof(buf));
}

static void child_case_no_delimiter(void)
{
    uint8_t *buf = malloc(16);

    if (buf == NULL)
        _exit(125);

    memset(buf, 0xff, 16);
    buf[14] = RSA_PKCS1_PADDING;
    buf[15] = 0x00;

    (void)run_case("unterminated-ff-padding", buf, 16);
    free(buf);
}

static void child_case_too_small(void)
{
    uint8_t *buf = malloc(1);

    if (buf == NULL)
        _exit(125);

    buf[0] = 0x00;

    (void)run_case("too-small-size", buf, 1);
    free(buf);
}

static int run_child(const char *name, void (*fn)(void))
{
    pid_t pid = fork();
    int status = 0;

    if (pid < 0)
    {
        perror("fork");
        return 1;
    }

    if (pid == 0)
    {
        fn();
        _exit(0);
    }

    if (waitpid(pid, &status, 0) < 0)
    {
        perror("waitpid");
        return 1;
    }

    if (WIFSIGNALED(status))
    {
        printf("summary case=%s signal=%d\n", name, WTERMSIG(status));
        return 1;
    }

    printf("summary case=%s exit=%d\n", name, WEXITSTATUS(status));
    return WEXITSTATUS(status) == 0 ? 0 : 1;
}

int main(void)
{
    int failures = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    failures += run_child("valid-shaped", child_case_valid);
    failures += run_child("unterminated-ff-padding", child_case_no_delimiter);
    failures += run_child("too-small-size", child_case_too_small);

    printf("overall child_failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
