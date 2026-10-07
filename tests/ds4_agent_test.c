#define DS4_AGENT_TEST
#define DS4_AGENT_TEST_NO_MAIN
#include "../ds4_agent.c"
#include <sys/resource.h>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/xattr.h>
#endif

static const char *test_output_dir;

static void test_agent_cli(void) {
    char *bare[] = {"ds4"};
    agent_config cfg = parse_options(1, bare);
    AGENT_TEST_ASSERT(!cfg.non_interactive && !cfg.gen.prompt && !cfg.chdir_path);
    char *interactive[] = {"ds4", "Explain this project", "-C", "project dir"};
    cfg = parse_options(4, interactive);
    AGENT_TEST_ASSERT(!cfg.non_interactive);
    AGENT_TEST_ASSERT(!strcmp(cfg.gen.prompt, "Explain this project"));
    AGENT_TEST_ASSERT(!strcmp(cfg.chdir_path, "project dir"));
    char *once[] = {"ds4", "--cd", "project dir", "exec", "Summarize", "--nothink"};
    cfg = parse_options(6, once);
    AGENT_TEST_ASSERT(cfg.non_interactive && !strcmp(cfg.gen.prompt, "Summarize"));
    AGENT_TEST_ASSERT(!strcmp(cfg.chdir_path, "project dir"));
    AGENT_TEST_ASSERT(cfg.gen.think_mode == DS4_THINK_NONE);
    char *literal[] = {"ds4", "e", "--", "--help"};
    cfg = parse_options(4, literal);
    AGENT_TEST_ASSERT(cfg.non_interactive && !strcmp(cfg.gen.prompt, "--help"));
    char *reserved[] = {"ds4", "--", "exec"};
    cfg = parse_options(3, reserved);
    AGENT_TEST_ASSERT(!cfg.non_interactive && !strcmp(cfg.gen.prompt, "exec"));
    char *no_mtp[] = {"ds4", "--no-mtp"};
    cfg = parse_options(2, no_mtp);
    AGENT_TEST_ASSERT(cfg.no_mtp && !cfg.engine.dspark && !cfg.engine.mtp_path);

    char *invalid[][5] = {
        {"ds4", "exec", NULL},
        {"ds4", "exec", "", NULL},
        {"ds4", "first", "second", NULL},
        {"ds4", "first", "-p", "second", NULL},
        {"ds4", "-C", NULL},
        {"ds4", "--no-mtp", "--dspark", NULL},
        {"ds4", "--mtp", "--no-mtp", NULL},
        {"ds4", "--no-mtp", "--mtp-model", "draft.gguf", NULL},
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        fflush(NULL);
        pid_t child = fork();
        AGENT_TEST_ASSERT(child >= 0);
        if (child == 0) {
            if (!freopen("/dev/null", "w", stderr)) _exit(1);
            int argc = 0;
            while (invalid[i][argc]) argc++;
            parse_options(argc, invalid[i]);
            _exit(0);
        }
        int status = 0;
        if (child > 0) waitpid(child, &status, 0);
        AGENT_TEST_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 2);
    }
}

static void test_agent_runtime(void) {
    char cwd[PATH_MAX], after[PATH_MAX];
    AGENT_TEST_ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    char fixture[] = "tests/.agent-runtime-XXXXXX";
    AGENT_TEST_ASSERT(mkdtemp(fixture) != NULL);
    char *root = realpath(fixture, NULL);
    AGENT_TEST_ASSERT(root != NULL);
    if (!root) return;
    char *bin = ds4_kvstore_path_join(root, "bin");
    char *share = ds4_kvstore_path_join(root, "share");
    char *runtime = ds4_kvstore_path_join(share, "ds4");
    char *executable = ds4_kvstore_path_join(bin, "ds4");
    char *link = ds4_kvstore_path_join(root, "agent-link");
    AGENT_TEST_ASSERT(mkdir(bin, 0700) == 0);
    FILE *fp = fopen(executable, "w");
    AGENT_TEST_ASSERT(fp != NULL);
    if (fp) fclose(fp);
    char *found = agent_runtime_directory(executable);
    AGENT_TEST_ASSERT(found && !strncmp(found, bin, strlen(bin)));
    free(found);
    AGENT_TEST_ASSERT(mkdir(share, 0700) == 0);
    AGENT_TEST_ASSERT(mkdir(runtime, 0700) == 0);
    AGENT_TEST_ASSERT(symlink(executable, link) == 0);
    found = agent_runtime_directory(link);
    AGENT_TEST_ASSERT(found && !strcmp(found, runtime));

    char *saved_model = getenv("DS4_MODEL") ? xstrdup(getenv("DS4_MODEL")) : NULL;
    char *saved_metal = getenv("DS4_METAL_SOURCE_DIR") ? xstrdup(getenv("DS4_METAL_SOURCE_DIR")) : NULL;
    unsetenv("DS4_MODEL");
    unsetenv("DS4_METAL_SOURCE_DIR");
    agent_config cfg = {0};
    agent_configure_runtime(&cfg, found);
    char *model = ds4_kvstore_path_join(runtime, "ds4flash.gguf");
    char *metal = ds4_kvstore_path_join(runtime, "metal");
    AGENT_TEST_ASSERT(!strcmp(cfg.engine.model_path, model));
    AGENT_TEST_ASSERT(!cfg.engine.vision_path);
    AGENT_TEST_ASSERT(!strcmp(getenv("DS4_METAL_SOURCE_DIR"), metal));
    free(cfg.model_path_owned);
    char *vision = ds4_kvstore_path_join(runtime, "ds4vision.gguf");
    fp = fopen(vision, "w");
    AGENT_TEST_ASSERT(fp != NULL);
    if (fp) fclose(fp);
    cfg = (agent_config){0};
    agent_configure_runtime(&cfg, found);
    AGENT_TEST_ASSERT(cfg.engine.vision_path && !strcmp(cfg.engine.vision_path, vision));
    free(cfg.model_path_owned);
    free(cfg.vision_path_owned);
    cfg = (agent_config){.engine.vision_path = "explicit-encoder.gguf"};
    agent_configure_runtime(&cfg, found);
    AGENT_TEST_ASSERT(!strcmp(cfg.engine.vision_path, "explicit-encoder.gguf"));
    AGENT_TEST_ASSERT(!cfg.vision_path_owned);
    free(cfg.model_path_owned);
    setenv("DS4_MODEL", "custom.gguf", 1);
    setenv("DS4_METAL_SOURCE_DIR", "custom-metal", 1);
    cfg = (agent_config){0};
    agent_configure_runtime(&cfg, found);
    AGENT_TEST_ASSERT(!strcmp(cfg.engine.model_path, "custom.gguf"));
    AGENT_TEST_ASSERT(!cfg.engine.vision_path);
    AGENT_TEST_ASSERT(!strcmp(getenv("DS4_METAL_SOURCE_DIR"), "custom-metal"));
    free(cfg.model_path_owned);
    cfg = (agent_config){.engine.model_path = "explicit.gguf"};
    agent_configure_runtime(&cfg, found);
    AGENT_TEST_ASSERT(!strcmp(cfg.engine.model_path, "explicit.gguf"));
    AGENT_TEST_ASSERT(!cfg.engine.vision_path);
    AGENT_TEST_ASSERT(getcwd(after, sizeof(after)) && !strcmp(cwd, after));
    char *mtp = ds4_kvstore_path_join(runtime, "ds4dspark.gguf");
    fp = fopen(mtp, "w");
    AGENT_TEST_ASSERT(fp != NULL);
    if (fp) fclose(fp);
    cfg = (agent_config){0};
    agent_configure_runtime(&cfg, found);
    AGENT_TEST_ASSERT(!cfg.engine.mtp_path && !cfg.engine.dspark);
    free(cfg.model_path_owned);
    unsetenv("DS4_MODEL");
    cfg = (agent_config){0};
    agent_configure_runtime(&cfg, found);
    AGENT_TEST_ASSERT(cfg.engine.dspark && cfg.engine.mtp_path &&
                      !strcmp(cfg.engine.mtp_path, mtp));
    free(cfg.model_path_owned); free(cfg.vision_path_owned); free(cfg.mtp_path_owned);
    agent_config overrides[] = {
        {.no_mtp = true},
        {.engine.mtp_path = "explicit-drafter.gguf"},
        {.engine.glm_mtp = true},
        {.engine.ngram_spec_draft_tokens = 5},
        {.engine.model_path = "explicit.gguf"},
    };
    for (size_t i = 0; i < sizeof(overrides) / sizeof(*overrides); i++) {
        agent_configure_runtime(&overrides[i], found);
        AGENT_TEST_ASSERT(!overrides[i].mtp_path_owned && !overrides[i].engine.dspark);
        if (i == 1) AGENT_TEST_ASSERT(!strcmp(overrides[i].engine.mtp_path, "explicit-drafter.gguf"));
        free(overrides[i].model_path_owned);
        free(overrides[i].vision_path_owned);
    }
    if (saved_model) setenv("DS4_MODEL", saved_model, 1); else unsetenv("DS4_MODEL");
    if (saved_metal) setenv("DS4_METAL_SOURCE_DIR", saved_metal, 1); else unsetenv("DS4_METAL_SOURCE_DIR");

    unlink(link);
    unlink(executable);
    unlink(vision);
    unlink(mtp);
    rmdir(runtime);
    rmdir(share);
    rmdir(bin);
    rmdir(root);
    free(saved_model); free(saved_metal); free(found); free(model); free(metal);
    free(vision);
    free(mtp);
    free(link); free(executable); free(runtime); free(share); free(bin); free(root);
}

static void test_fixture(const char *name, const char *data, size_t len) {
    if (!test_output_dir) return;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", test_output_dir, name);
    FILE *fp = fopen(path, "wb");
    AGENT_TEST_ASSERT(fp != NULL);
    if (!fp) return;
    AGENT_TEST_ASSERT(fwrite(data, 1, len, fp) == len);
    AGENT_TEST_ASSERT(fclose(fp) == 0);
}

static void test_tool_arg(agent_tool_call *call, const char *name, const char *value) {
    agent_tool_call_add_arg(call, name, value, strlen(value), true, "</arg_value>");
}

static int test_write_file(const char *path, const char *data, size_t len,
                           char *err, size_t errlen) {
    return agent_replace_file(path, data, len, NULL, 0, err, errlen);
}

static void test_atomic_file_tools(void) {
    char dir[] = "/tmp/ds4-agent-files-XXXXXX";
    AGENT_TEST_ASSERT(mkdtemp(dir) != NULL);
    char path[PATH_MAX], linkpath[PATH_MAX], err[256];
    snprintf(path, sizeof(path), "%s/file", dir);
    snprintf(linkpath, sizeof(linkpath), "%s/link", dir);
    char original[4096];
    memset(original, 'x', sizeof(original));
    AGENT_TEST_ASSERT(test_write_file(path, original, sizeof(original), err, sizeof(err)) == 0);
    AGENT_TEST_ASSERT(chmod(path, 0751) == 0);
#ifdef __APPLE__
    AGENT_TEST_ASSERT(setxattr(path, "com.ds4.agent-test", "keep", 4, 0, 0) == 0);
#elif defined(__linux__)
    AGENT_TEST_ASSERT(setxattr(path, "user.ds4-agent-test", "keep", 4, 0) == 0);
#endif

    pid_t child = fork();
    AGENT_TEST_ASSERT(child >= 0);
    if (child == 0) {
        signal(SIGXFSZ, SIG_IGN);
        struct rlimit limit = {64, 64};
        if (setrlimit(RLIMIT_FSIZE, &limit)) _exit(2);
        int rc = agent_replace_file(path, original, sizeof(original),
                                     original, sizeof(original), err, sizeof(err));
        _exit(rc == -1 ? 0 : 3);
    }
    int status = 0;
    if (child > 0) waitpid(child, &status, 0);
    AGENT_TEST_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    char *data = NULL;
    size_t len = 0;
    AGENT_TEST_ASSERT(agent_read_file_bytes(path, &data, &len, err, sizeof(err)) == 0);
    AGENT_TEST_ASSERT(len == sizeof(original) && !memcmp(data, original, len));
    free(data);
    AGENT_TEST_ASSERT(agent_replace_file(path, "bad", 3, "stale", 5, err, sizeof(err)) == -1);
    AGENT_TEST_ASSERT(strstr(err, "changed") != NULL);

    AGENT_TEST_ASSERT(symlink("file", linkpath) == 0);
    AGENT_TEST_ASSERT(test_write_file(linkpath, "new", 3, err, sizeof(err)) == 0);
    struct stat st;
    AGENT_TEST_ASSERT(lstat(linkpath, &st) == 0 && S_ISLNK(st.st_mode));
    AGENT_TEST_ASSERT(stat(path, &st) == 0 && (st.st_mode & 0777) == 0751);
    AGENT_TEST_ASSERT(st.st_uid == getuid());
#ifdef __APPLE__
    char attribute[16];
    AGENT_TEST_ASSERT(getxattr(path, "com.ds4.agent-test", attribute, sizeof(attribute), 0, 0) == 4);
    AGENT_TEST_ASSERT(!memcmp(attribute, "keep", 4));
#elif defined(__linux__)
    char attribute[16];
    AGENT_TEST_ASSERT(getxattr(path, "user.ds4-agent-test", attribute, sizeof(attribute)) == 4);
    AGENT_TEST_ASSERT(!memcmp(attribute, "keep", 4));
#endif
    AGENT_TEST_ASSERT(agent_read_file_bytes(path, &data, &len, err, sizeof(err)) == 0);
    AGENT_TEST_ASSERT(len == 3 && !memcmp(data, "new", 3));
    free(data);
    unlink(linkpath);
    AGENT_TEST_ASSERT(link(path, linkpath) == 0);
    AGENT_TEST_ASSERT(test_write_file(path, "bad", 3, err, sizeof(err)) == -1);
    AGENT_TEST_ASSERT(strstr(err, "hard-linked") != NULL);
    unlink(linkpath);
    unlink(path);
    /* Failed replacements must not leave temporary files behind. */
    AGENT_TEST_ASSERT(rmdir(dir) == 0);

    const char *match = NULL;
    size_t match_len = 0;
    bool anchored = true;
    AGENT_TEST_ASSERT(agent_edit_find_old_span("literal [upto] text", 19,
                         "[upto]", false, &match, &match_len, &anchored, err, sizeof(err)));
    AGENT_TEST_ASSERT(!anchored && match_len == 6 && !memcmp(match, "[upto]", 6));
}

static void test_streaming_file_tools(void) {
    char path[] = "/tmp/ds4-agent-read-XXXXXX";
    int fd = mkstemp(path);
    AGENT_TEST_ASSERT(fd >= 0);
    FILE *fp = fdopen(fd, "wb");
    /* Larger than the old whole-file cap, with matches at both ends. */
    fputs("needle first\r\n", fp);
    for (int i = 0; i < 1024 * 1024; i++) fputs("0123456789abcdef\n", fp);
    fputs("needle last\r", fp);
    fclose(fp);
    agent_worker w = {0};
    char *text = agent_read_range(&w, path, 1, 1, false, false, true);
    AGENT_TEST_ASSERT(strstr(text, "needle first") && w.more_valid);
    AGENT_TEST_ASSERT(w.more_next_line == 2 && w.more_byte_offset == 14);
    free(text);
    agent_tool_call more = {0};
    test_tool_arg(&more, "count", "1");
    text = agent_tool_more(&w, &more);
    AGENT_TEST_ASSERT(strstr(text, "2 0123456789abcdef"));
    free(text);
    agent_tool_call_free(&more);

    agent_tool_call call = {0};
    test_tool_arg(&call, "path", path);
    test_tool_arg(&call, "query", "needle");
    char linkpath[PATH_MAX];
    snprintf(linkpath, sizeof(linkpath), "%s-link", path);
    AGENT_TEST_ASSERT(symlink(path, linkpath) == 0);
    agent_tool_call linked = {0};
    test_tool_arg(&linked, "path", linkpath);
    test_tool_arg(&linked, "query", "needle");
    text = agent_tool_search(&w, &linked);
    AGENT_TEST_ASSERT(strstr(text, "2 matches") && strstr(text, "needle last"));
    free(text);
    agent_tool_call_free(&linked);
    unlink(linkpath);
    text = agent_tool_search(&w, &call);
    AGENT_TEST_ASSERT(strstr(text, "2 matches") && strstr(text, "needle last"));
    free(text);
    test_tool_arg(&call, "mode", "regexp");
    text = agent_tool_search(&w, &call);
    AGENT_TEST_ASSERT(strstr(text, "Tool error:"));
    free(text);
    agent_tool_call_free(&call);

    fp = fopen(path, "wb");
    for (int i = 0; i < 256 * 1024; i++) fputc('x', fp);
    fclose(fp);
    size_t total = 0;
    text = agent_read_range(&w, path, 1, 1, false, true, true);
    do {
        size_t n = strspn(text, "x");
        total += n;
        AGENT_TEST_ASSERT(n > 0 && n < AGENT_TOOL_MAX_BYTES);
        if (w.more_valid) AGENT_TEST_ASSERT(strstr(text, "Read truncated") != NULL);
        free(text);
        if (!w.more_valid) break;
        text = agent_tool_more(&w, &more);
    } while (total < 512 * 1024);
    AGENT_TEST_ASSERT(total == 256 * 1024 && !w.more_valid);
    text = agent_read_range(&w, path, 1, INT_MAX, true, true, true);
    AGENT_TEST_ASSERT(strstr(text, "Tool error: whole read") && !w.more_valid);
    free(text);
    fp = fopen(path, "wb");
    for (int i = 0; i < 256 * 1024; i++) fputc(0x80, fp);
    fclose(fp);
    text = agent_read_range(&w, path, 1, 1, false, true, true);
    AGENT_TEST_ASSERT(strlen(text) < AGENT_TOOL_MAX_BYTES && w.more_valid);
    free(text);
    unlink(path);
    AGENT_TEST_ASSERT(mkfifo(path, 0600) == 0);
    double started = now_sec();
    text = agent_read_range(&w, path, 1, 1, false, false, true);
    AGENT_TEST_ASSERT(strstr(text, "Tool error:") && now_sec() - started < 0.5);
    free(text);
    unlink(path);
    test_tool_arg(&call, "path", path);
    test_tool_arg(&call, "query", "needle");
    text = agent_tool_search(&w, &call);
    AGENT_TEST_ASSERT(strstr(text, "Tool error:"));
    free(text);
    agent_tool_call_free(&call);

    agent_buf b = {0};
    char *large = xmalloc(200000);
    memset(large, 'q', 200000);
    agent_buf_append(&b, large, 200000);
    text = agent_buf_take(&b);
    AGENT_TEST_ASSERT(strlen(text) == 200000);
    free(text);
    b.limit = 100;
    agent_buf_append(&b, large, 200000);
    text = agent_buf_take(&b);
    AGENT_TEST_ASSERT(strstr(text, "Output truncated") != NULL);
    free(large);
    free(text);
    b.limit = 3;
    agent_buf_puts(&b, "a\xe4\xb8\xad" "b");
    text = agent_buf_take(&b);
    AGENT_TEST_ASSERT(!strncmp(text, "a\n[Output truncated", 19));
    free(text);
}

static void test_shell_spawn(void) {
    agent_worker w = {0};
    pthread_mutex_init(&w.mu, NULL);
    w.wake_fd[0] = w.wake_fd[1] = -1;
    char err[256], cwd[PATH_MAX];
    AGENT_TEST_ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    setenv("DS4_TEST_SHELL_ENV", "inherited", 1);
    for (int closed_stdio = 0; closed_stdio <= 1; closed_stdio++) {
        int saved[3];
        if (closed_stdio) {
            fflush(NULL);
            for (int i = 0; i < 3; i++) {
                saved[i] = fcntl(i, F_DUPFD_CLOEXEC, 3);
                AGENT_TEST_ASSERT(saved[i] >= 0);
                if (saved[i] < 0) exit(1);
            }
            for (int i = 0; i < 3; i++) close(i);
        }
        agent_bash_job *job = agent_bash_start(&w,
            "read line || printf 'stdin-eof\\n'; pwd; "
            "printf 'env=%s\\n' \"$DS4_TEST_SHELL_ENV\"; "
            "printf 'stderr-captured\\n' >&2; sleep 0.1; exit 23",
            5, err, sizeof(err));
        bool group_ok = job && getpgid(job->pid) == job->pid;
        double start = now_sec();
        while (job && agent_bash_is_running(job) && now_sec() - start < 6)
            usleep(10000);
        bool finished = false;
        char *obs = job ? agent_bash_observation(job, true, &finished) : NULL;
        if (job) {
            unlink(job->path);
            agent_bash_remove_job(&w, job);
        }
        if (closed_stdio) {
            for (int i = 0; i < 3; i++) {
                if (dup2(saved[i], i) < 0) _exit(1);
                close(saved[i]);
            }
        }
        AGENT_TEST_ASSERT(group_ok && finished && obs);
        if (obs) {
            AGENT_TEST_ASSERT(strstr(obs, "exit_status=23"));
            AGENT_TEST_ASSERT(strstr(obs, "stdin-eof"));
            AGENT_TEST_ASSERT(strstr(obs, cwd));
            AGENT_TEST_ASSERT(strstr(obs, "env=inherited"));
            AGENT_TEST_ASSERT(strstr(obs, "stderr-captured"));
        }
        free(obs);
    }
    unsetenv("DS4_TEST_SHELL_ENV");
    free(w.out);
    pthread_mutex_destroy(&w.mu);
}

static void test_background_jobs(void) {
    agent_worker w = {0};
    pthread_mutex_init(&w.mu, NULL);
    w.wake_fd[0] = w.wake_fd[1] = -1;
    char err[256], marker[] = "/tmp/ds4-agent-deadline-XXXXXX";
    int fd = mkstemp(marker);
    close(fd);
    unlink(marker);
    char cmd[PATH_MAX + 128];
    snprintf(cmd, sizeof(cmd), "sleep 2; printf late > %s", marker);
    agent_bash_job *job = agent_bash_start(&w, cmd, 1, err, sizeof(err));
    AGENT_TEST_ASSERT(job != NULL);
    if (!job) goto done;
    /* Deliberately no status polling: this stands in for model generation. */
    usleep(2400000);
    AGENT_TEST_ASSERT(access(marker, F_OK) != 0);
    bool finished = false;
    char *obs = agent_bash_observation(job, true, &finished);
    AGENT_TEST_ASSERT(finished && strstr(obs, "timed_out=1"));
    free(obs);
    unlink(job->path);
    agent_bash_remove_job(&w, job);

    job = agent_bash_start(&w, "(sleep 0.1; printf descendant-output) &", 5, err, sizeof(err));
    AGENT_TEST_ASSERT(job != NULL);
    if (!job) goto done;
    usleep(350000);
    obs = agent_bash_observation(job, true, &finished);
    AGENT_TEST_ASSERT(finished && strstr(obs, "descendant-output"));
    free(obs);
    unlink(job->path);
    agent_bash_remove_job(&w, job);

    job = agent_bash_start(&w, "head -c 2097152 /dev/zero | tr '\\000' x", 5, err, sizeof(err));
    AGENT_TEST_ASSERT(job != NULL);
    if (!job) goto done;
    usleep(900000);
    obs = agent_bash_observation(job, true, &finished);
    AGENT_TEST_ASSERT(finished && strstr(obs, "exit_status=0"));
    AGENT_TEST_ASSERT(job->bytes == 2097152);
    free(obs);
    obs = agent_bash_observation(job, true, &finished);
    AGENT_TEST_ASSERT(strlen(obs) < AGENT_BASH_TAIL_BYTES + 2048);
    free(obs);
    unlink(job->path);
    agent_bash_remove_job(&w, job);

    job = agent_bash_start(&w, "sleep 0.3; printf completed", 5, err, sizeof(err));
    AGENT_TEST_ASSERT(job != NULL);
    if (!job) goto done;
    char output_path[PATH_MAX];
    snprintf(output_path, sizeof(output_path), "%s", job->path);
    agent_tool_call call = {.name = xstrdup("bash_status")};
    char id[32];
    snprintf(id, sizeof(id), "%d", job->id);
    test_tool_arg(&call, "job", id);
    test_tool_arg(&call, "refresh_sec", "1");
    double start = now_sec();
    obs = agent_execute_tool_call(&w, &call);
    AGENT_TEST_ASSERT(now_sec() - start >= 0.2);
    AGENT_TEST_ASSERT(strstr(obs, "status=done") && strstr(obs, "completed"));
    AGENT_TEST_ASSERT(w.bash_jobs == NULL);
    free(obs);
    agent_tool_call_free(&call);
    unlink(output_path);

    /* Two monitors must make progress together, and stopping one must neither
     * block shutdown nor kill the other job's process group. */
    job = agent_bash_start(&w, "sleep 30", 60, err, sizeof(err));
    agent_bash_job *other = agent_bash_start(&w, "sleep 0.2; printf independent", 5, err, sizeof(err));
    AGENT_TEST_ASSERT(job && other);
    if (!job || !other) goto done;
    start = now_sec();
    agent_bash_signal(job, SIGKILL);
    unlink(job->path);
    agent_bash_remove_job(&w, job);
    AGENT_TEST_ASSERT(now_sec() - start < 2);
    while (agent_bash_is_running(other) && now_sec() - start < 3) usleep(10000);
    obs = agent_bash_observation(other, true, &finished);
    AGENT_TEST_ASSERT(finished && strstr(obs, "exit_status=0") && strstr(obs, "independent"));
    free(obs);
    unlink(other->path);
    agent_bash_remove_job(&w, other);
done:
    agent_bash_jobs_free(&w);
    unlink(marker);
    free(w.out);
    pthread_mutex_destroy(&w.mu);
}

static void test_completion(const char *text, linenoiseCompletions *completions) {
    (void)text;
    linenoiseAddCompletion(completions, "example");
}

static void test_fragmented_terminal_input(void) {
    int input[2];
    AGENT_TEST_ASSERT(pipe(input) == 0);
    fcntl(input[0], F_SETFL, O_NONBLOCK);
    FILE *sink = tmpfile();
    AGENT_TEST_ASSERT(sink != NULL);
    if (!sink) { close(input[0]); close(input[1]); return; }
    setenv("LINENOISE_ASSUME_TTY", "1", 1);
    struct linenoiseState l = {0};
    char buffer[1024] = "";
    l.ifd = input[0]; l.ofd = fileno(sink);
    l.buf = buffer; l.buflen = sizeof(buffer) - 1;
    l.cols = 80; l.prompt = "";
    const char *samples[] = {"\xc3\xa9", "\xe4\xb8\xad", "\xf0\x9f\x98\x80"};
    for (size_t s = 0; s < sizeof(samples)/sizeof(samples[0]); s++) {
        const char *sample = samples[s];
        for (size_t split = 1; split < strlen(sample); split++) {
            linenoiseEditClear(&l);
            for (size_t i = 0; i < split; i++)
                AGENT_TEST_ASSERT(linenoiseEditFeedByte(&l, sample[i]) == linenoiseEditMore);
            AGENT_TEST_ASSERT(l.len == 0);
            AGENT_TEST_ASSERT(linenoiseEditFeed(&l) == linenoiseEditMore);
            AGENT_TEST_ASSERT(l.len == 0);
            for (size_t i = split; i < strlen(sample); i++)
                AGENT_TEST_ASSERT(linenoiseEditFeedByte(&l, sample[i]) == linenoiseEditMore);
            AGENT_TEST_ASSERT(!strcmp(l.buf, sample));
        }
    }
    linenoiseEditClear(&l);
    const char sequence[] = "ab\x1b[DZ\x1b[3~\x1b[H!";
    for (size_t i = 0; i < sizeof(sequence) - 1; i++) {
        AGENT_TEST_ASSERT(linenoiseEditFeedByte(&l, sequence[i]) == linenoiseEditMore);
        AGENT_TEST_ASSERT(linenoiseEditFeed(&l) == linenoiseEditMore);
    }
    AGENT_TEST_ASSERT(!strcmp(l.buf, "!aZ"));
    linenoiseEditClear(&l);
    const char paste[] = "\x1b[200~one\r\n\xe4\xb8\xad\nthree\x1b[201~";
    for (size_t i = 0; i < sizeof(paste) - 1; i++) {
        AGENT_TEST_ASSERT(linenoiseEditFeedByte(&l, paste[i]) == linenoiseEditMore);
        AGENT_TEST_ASSERT(linenoiseEditFeed(&l) == linenoiseEditMore);
        if (i < sizeof(paste) - 2) AGENT_TEST_ASSERT(l.len == 0);
    }
    AGENT_TEST_ASSERT(!strcmp(l.buf, "one\n\xe4\xb8\xad\nthree"));
    linenoiseEditClear(&l);
    linenoiseEditFeedByte(&l, '\xe4');
    linenoiseEditFeedByte(&l, 'X');
    AGENT_TEST_ASSERT(!strcmp(l.buf, "\xef\xbf\xbdX"));
    linenoiseEditClear(&l);
    const char invalid_paste[] = "\x1b[200~bad\xe4\x1b[201~";
    for (size_t i = 0; i < sizeof(invalid_paste) - 1; i++)
        AGENT_TEST_ASSERT(linenoiseEditFeedByte(&l, invalid_paste[i]) == linenoiseEditMore);
    AGENT_TEST_ASSERT(l.len == 0 && !l.paste_active);
    linenoiseSetCompletionCallback(test_completion);
    linenoiseEditFeedByte(&l, 'e');
    linenoiseEditFeedByte(&l, '\t');
    AGENT_TEST_ASSERT(l.in_completion);
    linenoiseEditFeedByte(&l, '\x1b');
    AGENT_TEST_ASSERT(!l.in_completion && !strcmp(l.buf, "e"));
    linenoiseEditFeedByte(&l, '[');
    linenoiseEditFeedByte(&l, 'D');
    AGENT_TEST_ASSERT(l.pos == 0);
    linenoiseEditClear(&l);
    linenoiseEditFeedByte(&l, '\t');
    const char unicode[] = "\xe4\xb8\xad";
    for (size_t i = 0; i < sizeof(unicode) - 1; i++) linenoiseEditFeedByte(&l, unicode[i]);
    AGENT_TEST_ASSERT(!l.in_completion && !strcmp(l.buf, "example\xe4\xb8\xad"));
    linenoiseEditClear(&l);
    l.buflen = 3;
    linenoiseEditFeedByte(&l, 'e');
    linenoiseEditFeedByte(&l, '\t');
    linenoiseEditFeedByte(&l, ' ');
    AGENT_TEST_ASSERT(!l.in_completion && !strcmp(l.buf, "e") && l.len == 1);
    l.buflen = sizeof(buffer) - 1;
    linenoiseSetCompletionCallback(NULL);
    free(l.queued_input);
    free(l.paste_buf);
    close(input[0]); close(input[1]); fclose(sink);
    unsetenv("LINENOISE_ASSUME_TTY");
}

static void test_shell_terminal_controls(void) {
    const char malicious[] = "before\x1b[2Jafter\x1b[H!\x1b]52;c;secret\a"
                             "\x1bPdata\x1b\\\x1b[31mred\x1b[0m\b\n";
    char *safe = agent_terminal_safe_text(malicious, sizeof(malicious) - 1);
    AGENT_TEST_ASSERT(!strcmp(safe, "beforeafter!\x1b[31mred\x1b[0m\\x08\n"));
    free(safe);
    const char c1[] = "\xe4\xb8\xad\xc2\x9b" "2J\x9b" "2J";
    safe = agent_terminal_safe_text(c1, sizeof(c1) - 1);
    AGENT_TEST_ASSERT(!strcmp(safe, "\xe4\xb8\xad\\xc2\\x9b2J\\x9b2J"));
    free(safe);
    for (size_t i = 0; i < sizeof(malicious); i++) {
        safe = agent_terminal_safe_text(malicious, i);
        AGENT_TEST_ASSERT(!strstr(safe, "\x1b[2J") && !strstr(safe, "\x1b]52"));
        free(safe);
    }
}

static void test_markdown_literals(void) {
    const char *input[] = {"Use *.c files.", "The literal is \\*.", "An unmatched `tick",
                          "**bold** and *italic* and `code`.", "``a ` b``", "*unclosed",
                          "* list item\n", "trailing \\", "**unclosed", "`a``", "\\`literal\\`",
                          "> **Hint:** Check `errno`.\n"};
    const char *expected[] = {"Use *.c files.", "The literal is *.", "An unmatched `tick",
                             "bold and italic and code.", "a ` b", "*unclosed",
                             "* list item\n", "trailing \\", "**unclosed", "`a``", "`literal`",
                             "> Hint: Check errno.\n"};
    for (size_t i = 0; i < sizeof(input)/sizeof(input[0]); i++) {
        agent_tail_capture capture = {.cap = 16384};
        agent_token_renderer r = {.capture = &capture, .format_markdown = true};
        for (size_t j = 0; j < strlen(input[i]); j++) renderer_markdown_feed(&r, input[i][j]);
        renderer_markdown_finish(&r);
        renderer_flush_utf8(&r);
        size_t len;
        char *out = agent_tail_capture_take(&capture, &len);
        AGENT_TEST_ASSERT(!strcmp(out, expected[i]));
        if (strcmp(out, expected[i])) fprintf(stderr, "markdown: %s => %s\n", input[i], out);
        free(out);
    }
    agent_tail_capture capture = {.cap = 20000};
    agent_token_renderer r = {.capture = &capture, .format_markdown = true};
    renderer_markdown_feed(&r, '*');
    for (int i = 0; i < 8192; i++) renderer_markdown_feed(&r, 'x');
    renderer_markdown_finish(&r);
    size_t len;
    char *out = agent_tail_capture_take(&capture, &len);
    AGENT_TEST_ASSERT(len == 8193 && out[0] == '*');
    free(out);
}

static char *test_hint_capture(const char *text, size_t split, bool markdown,
                               bool color, agent_tool_syntax syntax, int *calls) {
    agent_tail_capture capture = {.cap = 32768};
    agent_token_renderer renderer = {.capture = &capture, .format_thinking = true,
        .format_markdown = markdown, .use_color = color, .last_output_newline = true};
    agent_dsml_parser parser = {.syntax = syntax, .state = AGENT_DSML_SEARCH};
    agent_stream_renderer stream = {.renderer = &renderer, .parser = &parser, .syntax = syntax};
    size_t n = strlen(text);
    if (split <= n) {
        agent_stream_text(&stream, text, split, false);
        /* A prompt redraw resets terminal colors between generated fragments. */
        if (color && renderer.wrote_visible_output) renderer_write(&renderer, "\x1b[0m", 4);
        agent_stream_text(&stream, text + split, n - split, false);
    } else {
        for (size_t i = 0; i < n; i++) {
            agent_stream_text(&stream, text + i, 1, false);
            if (color && renderer.wrote_visible_output) renderer_write(&renderer, "\x1b[0m", 4);
        }
    }
    agent_stream_text(&stream, NULL, 0, true);
    renderer_finish(&renderer);
    AGENT_TEST_ASSERT(!renderer.md_hint && !renderer.md_hint_prefix_len && !renderer.color_open);
    if (calls) {
        *calls = (int)parser.calls.len;
        AGENT_TEST_ASSERT(parser.calls.len == 1);
        if (parser.calls.len == 1) {
            AGENT_TEST_ASSERT(!strcmp(parser.calls.v[0].name, "bash"));
            AGENT_TEST_ASSERT(parser.calls.v[0].argc == 1);
            if (parser.calls.v[0].argc == 1)
                AGENT_TEST_ASSERT(!strcmp(parser.calls.v[0].args[0].value, "printf HINT_OK"));
        }
    } else AGENT_TEST_ASSERT(parser.calls.len == 0);
    agent_dsml_parser_free(&parser);
    return agent_tail_capture_take(&capture, NULL);
}

static void test_hint_rendering(void) {
    const char *badge = "\x1b[1;97;48;5;23m Hint \x1b[0m";
    const char *sample =
        "The error path is fixed.\n\n"
        "> **Hint:** Save `errno` before **cleanup**; calls can overwrite it.\n"
        "> Keep the original error for reporting.\n"
        "> Unicode stays intact: caf\xc3\xa9, \xe4\xb8\xad.\n"
        "Normal prose resumes here.\n\n"
        "```text\n> **Hint:** This is literal code.\n```\n"
        "Another normal line.\n";
    for (size_t split = 0; split <= strlen(sample) + 1; split++) {
        char *out = test_hint_capture(sample, split, true, true, AGENT_TOOL_SYNTAX_DSML, NULL);
        const char *label = strstr(out, badge);
        AGENT_TEST_ASSERT(label && !strstr(label + strlen(badge), badge));
        AGENT_TEST_ASSERT(strstr(out, "\x1b[1mcleanup") || split > strlen(sample));
        if (split == strlen(sample)) test_fixture("hints.ansi", out, strlen(out));
        if (split > strlen(sample)) test_fixture("hints-fragmented.ansi", out, strlen(out));
        free(out);
    }
    const char *literal[] = {"> quoted text", "> **Hinting:** not a hint",
        "Inline > **Hint:** not an aside", "`> **Hint:** literal`",
        "\\> **Hint:** escaped", "<think>> **Hint:** hidden reasoning</think>Normal."};
    for (size_t i = 0; i < sizeof(literal) / sizeof(literal[0]); i++) {
        char *out = test_hint_capture(literal[i], strlen(literal[i]), true, true,
                                      AGENT_TOOL_SYNTAX_DSML, NULL);
        AGENT_TEST_ASSERT(!strstr(out, badge));
        free(out);
    }
    const char marker[] = "> **Hint:**";
    for (size_t i = 0; i < sizeof(marker) - 1; i++) {
        char partial[sizeof(marker)];
        memcpy(partial, marker, i);
        partial[i] = 0;
        char *out = test_hint_capture(partial, i, true, true, AGENT_TOOL_SYNTAX_GLM, NULL);
        AGENT_TEST_ASSERT(!strstr(out, badge) && !strncmp(out, partial, i));
        free(out);
    }
    char *out = test_hint_capture(sample, strlen(sample), false, false, AGENT_TOOL_SYNTAX_DSML, NULL);
    AGENT_TEST_ASSERT(!strncmp(out, sample, strlen(sample)) && !strchr(out, '\x1b'));
    free(out);
    out = test_hint_capture("> **Hint:** Check `errno`.\n", 0, true, false, AGENT_TOOL_SYNTAX_DSML, NULL);
    AGENT_TEST_ASSERT(!strcmp(out, "> Hint: Check errno.\n\n"));
    free(out);
    const char *tool[] = {
        "> **Hint:** Keep shell checks reproducible.\n"
        "<｜DSML｜tool_calls><｜DSML｜invoke name=\"bash\">"
        "<｜DSML｜parameter name=\"command\" string=\"true\">printf HINT_OK"
        "</｜DSML｜parameter></｜DSML｜invoke></｜DSML｜tool_calls>",
        "> **Hint:** Keep shell checks reproducible.\n"
        "<tool_call>bash<arg_key>command</arg_key><arg_value>printf HINT_OK</arg_value></tool_call>"
    };
    for (int glm = 0; glm < 2; glm++) {
        for (size_t split = 0; split <= strlen(tool[glm]); split++) {
            int calls = 0;
            out = test_hint_capture(tool[glm], split, true, true,
                glm ? AGENT_TOOL_SYNTAX_GLM : AGENT_TOOL_SYNTAX_DSML, &calls);
            AGENT_TEST_ASSERT(calls == 1 && strstr(out, badge));
            if (split == strlen(tool[glm]))
                test_fixture(glm ? "hints-glm-tool.ansi" : "hints-dsml-tool.ansi", out, strlen(out));
            free(out);
        }
    }
}

static void test_unicode_output_and_footer(void) {
    agent_editor ed = {0};
    ed.edit.cols = 80;
    char ascii[78];
    memset(ascii, 'a', sizeof(ascii));
    editor_note_output(&ed, ascii, sizeof(ascii));
    editor_note_output(&ed, "\xe4", 1);
    AGENT_TEST_ASSERT(ed.output_col == 78 && ed.output_utf8_len == 1);
    editor_note_output(&ed, "\xb8\xad", 2);
    AGENT_TEST_ASSERT(ed.output_col == 0 && ed.output_pending_wrap);
    editor_note_output(&ed, "\xcc\x81", 2);
    AGENT_TEST_ASSERT(ed.output_col == 0 && ed.output_pending_wrap);
    editor_note_output(&ed, "x", 1);
    AGENT_TEST_ASSERT(ed.output_col == 1 && !ed.output_pending_wrap);
    editor_note_output(&ed, "\r", 1);
    AGENT_TEST_ASSERT(ed.output_col == 0 && !ed.output_pending_wrap);
    const char family[] = "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";
    for (size_t i = 0; i < sizeof(family) - 1; i++) editor_note_output(&ed, family + i, 1);
    AGENT_TEST_ASSERT(ed.output_col == 2);
    int width;
    AGENT_TEST_ASSERT(linenoiseNextGrapheme(family, sizeof(family) - 1, &width) == sizeof(family) - 1 && width == 2);
    const char flag[] = "\xf0\x9f\x87\xae\xf0\x9f\x87\xb9";
    AGENT_TEST_ASSERT(linenoiseNextGrapheme(flag, sizeof(flag) - 1, &width) == sizeof(flag) - 1 && width == 2);
    editor_note_output(&ed, flag, sizeof(flag) - 1);
    AGENT_TEST_ASSERT(ed.output_col == 4);
    editor_note_output(&ed, "\x1b[", 2);
    AGENT_TEST_ASSERT(ed.output_escape == 2);
    editor_note_output(&ed, "31mX", 4);
    AGENT_TEST_ASSERT(ed.output_col == 5 && !ed.output_escape);

    agent_prompt_queue q = {0};
    char queued[181];
    for (int i = 0; i < 60; i++) memcpy(queued + i * 3, "\xe4\xb8\xad", 3);
    queued[180] = 0;
    agent_prompt_queue_push(&q, queued);
    agent_status st = {0};
    char footer[4096];
    build_footer_text(&st, &q, 40, footer, sizeof(footer));
    test_fixture("queue-footer.txt", footer, strlen(footer));
    for (size_t pos = 0; pos < strlen(footer);) {
        uint32_t cp;
        size_t n = linenoiseUtf8Decode(footer + pos, strlen(footer) - pos, &cp);
        AGENT_TEST_ASSERT(n && cp != 0xfffd);
        if (!n) break;
        pos += n;
    }
    agent_prompt_queue_free(&q);
}

static void test_footer_only_updates(void) {
    FILE *sink = tmpfile();
    AGENT_TEST_ASSERT(sink != NULL);
    if (!sink) return;
    int saved = dup(STDOUT_FILENO);
    dup2(fileno(sink), STDOUT_FILENO);
    agent_editor ed = {.active = true, .scroll_region = true, .term_rows = 24,
                       .term_cols = 80, .output_bottom = 22, .prompt_row = 23};
    snprintf(ed.prompt, sizeof(ed.prompt), "ds4-agent> ");
    snprintf(ed.status, sizeof(ed.status), "generation 0");
    char buffer[] = "draft";
    ed.edit = (struct linenoiseState){.ifd = -1, .ofd = STDOUT_FILENO,
        .buf = buffer, .buflen = sizeof(buffer), .len = 5, .pos = 5, .oldpos = 5,
        .prompt = ed.prompt, .plen = strlen(ed.prompt), .cols = 80,
        .oldrows = 1, .oldstatusrows = 1, .oldrpos = 1,
        .screen_cursor_row = 23, .screen_cursor_col = 17};
    linenoiseEditSetStatus(&ed.edit, ed.status, "", "");
    const char initial[] = "\x1b[23;1Hds4-agent> draft\r\ngeneration 0\x1b[23;17H";
    write_all(STDOUT_FILENO, initial, sizeof(initial) - 1);
    editor_set_prompt_status(&ed, ed.prompt, "generation 1");
    off_t first = lseek(STDOUT_FILENO, 0, SEEK_CUR);
    editor_set_prompt_status(&ed, ed.prompt, "generation 2");
    AGENT_TEST_ASSERT(lseek(STDOUT_FILENO, 0, SEEK_CUR) == first && ed.status_dirty);
    ed.last_prompt_redraw_time -= 1;
    editor_set_prompt_status(&ed, ed.prompt, "generation 2");
    AGENT_TEST_ASSERT(!ed.status_dirty);
    editor_set_prompt_status(&ed, ed.prompt, "done");
    editor_flush_prompt_status(&ed, true);
    AGENT_TEST_ASSERT(!ed.status_dirty && !strcmp(buffer, "draft"));
    dup2(saved, STDOUT_FILENO);
    close(saved);
    fseek(sink, 0, SEEK_END);
    size_t len = (size_t)ftell(sink);
    rewind(sink);
    char *text = xmalloc(len + 1);
    AGENT_TEST_ASSERT(fread(text, 1, len, sink) == len);
    text[len] = 0;
    AGENT_TEST_ASSERT(strstr(text, "\x1b[?2026h") && !strstr(text, "\x1b[0K"));
    test_fixture("status.ansi", text, len);
    free(text);
    free(ed.edit.status); free(ed.edit.status_start); free(ed.edit.status_end);
    fclose(sink);
}

static void test_tool_contracts(void) {
    for (int glm = 0; glm < 3; glm++) {
        for (int vision = 0; vision < 2; vision++) {
            char *prompt = glm == 2 ? agent_build_qwen_tools_prompt(false, vision) :
                           glm ? agent_build_glm_tools_prompt(false, vision) :
                                 agent_build_dsml_tools_prompt(false, vision);
            AGENT_TEST_ASSERT((strstr(prompt, "view_image") != NULL) == vision);
            AGENT_TEST_ASSERT(strstr(prompt, "POSIX extended") && strstr(prompt, "128 KiB"));
            AGENT_TEST_ASSERT(strstr(prompt, "&amp;lt;/"));
            char name[64];
            snprintf(name, sizeof(name), "prompt-%s-%d.txt", glm == 2 ? "qwen" : glm ? "glm" : "dsml", vision);
            test_fixture(name, prompt, strlen(prompt));
            free(prompt);
        }
    }
}

/* Model-free real-PTY driver for tests/ds4_agent_terminal_test.py. */
static int test_terminal_driver(void) {
    agent_editor ed = {0};
    linenoiseSetMultiLine(1);
    if (editor_start(&ed, "ds4-agent> ", "ready", NULL)) return 2;
    double start = now_sec(), next = start;
    char *answer = NULL;
    unsigned tick = 0;
    while (now_sec() - start < 10 && !answer) {
        struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
        poll(&pfd, 1, 10);
        if (pfd.revents & POLLIN) editor_read_stdin(&ed);
        while (linenoiseEditQueuedInput(&ed.edit)) {
            char *line = linenoiseEditFeed(&ed.edit);
            if (line == linenoiseEditMore) continue;
            if (line) answer = line;
            else answer = xstrdup("<input error>");
            break;
        }
        if (now_sec() >= next) {
            char status[80];
            snprintf(status, sizeof(status), "generation %u", ++tick);
            if (tick % 4 == 0) {
                const char output[] = "model output \xe4\xb8\xad\n";
                editor_write_async(&ed, output, sizeof(output) - 1, "ds4-agent> ", status, false);
            } else editor_set_prompt_status(&ed, "ds4-agent> ", status);
            next = now_sec() + 0.05;
        }
    }
    editor_stop(&ed);
    editor_restore_terminal_layout(&ed);
    if (!answer) return 3;
    printf("\nRESULT:");
    for (size_t i = 0; i < strlen(answer); i++) printf("%02x", (unsigned char)answer[i]);
    puts("");
    free(answer);
    return 0;
}

static void test_observation_error_is_not_context_exhaustion(void) {
    agent_worker worker = {0};
    ds4_tokens_push(&worker.transcript, 42);
    agent_tool_observation observation;
    agent_tool_observation_init(&observation);
    agent_tool_observation_puts(&observation, "image result");
    ds4_vision_embedding invalid = {0};
    agent_tool_observation_add_image(&observation, &invalid);
    char err[160] = {0};
    int count = -1;
    AGENT_TEST_ASSERT(agent_tool_observation_fits(&worker, &observation, 16,
                                                 &count, err, sizeof(err)) == -1);
    AGENT_TEST_ASSERT(strstr(err, "invalid image observation") != NULL);
    AGENT_TEST_ASSERT(count == -1);
    AGENT_TEST_ASSERT(worker.transcript.len == 1 && worker.transcript.v[0] == 42);
    agent_tool_observation_free(&observation);
    ds4_tokens_free(&worker.transcript);
}

static void test_compaction_boundaries(void) {
    agent_dsml_parser parser = {.state = AGENT_DSML_SEARCH};
    agent_stream_renderer stream = {.parser = &parser};
    AGENT_TEST_ASSERT(!agent_stream_compaction_needs_lookahead(&stream));
    stream.pending_len = 1;
    AGENT_TEST_ASSERT(agent_stream_compaction_needs_lookahead(&stream));
    stream.pending_len = 0;
    stream.dsml_start_len = 1;
    AGENT_TEST_ASSERT(agent_stream_compaction_needs_lookahead(&stream));
    stream.dsml_active = true;
    AGENT_TEST_ASSERT(!agent_stream_compaction_needs_lookahead(&stream));
    stream.dsml_active = false;
    parser.state = AGENT_DSML_PARAM_VALUE;
    AGENT_TEST_ASSERT(!agent_stream_compaction_needs_lookahead(&stream));
    int data[1000] = {0};
    ds4_tokens tokens = {.v = data, .len = 1000};
    AGENT_TEST_ASSERT(agent_compact_tail_boundary(&tokens, 1000, 100, 100, 42) == 900);
    data[850] = 42;
    AGENT_TEST_ASSERT(agent_compact_tail_boundary(&tokens, 1000, 100, 100, 42) == 850);
    data[950] = 42;
    AGENT_TEST_ASSERT(agent_compact_tail_boundary(&tokens, 1000, 100, 100, 42) == 950);
    data[850] = data[950] = 0;
    data[799] = 42;
    AGENT_TEST_ASSERT(agent_compact_tail_boundary(&tokens, 1000, 100, 100, 42) == 900);
    AGENT_TEST_ASSERT(agent_compact_tail_boundary(&tokens, 150, 100, 100, 42) == 100);
    AGENT_TEST_ASSERT(agent_compact_tail_boundary(&tokens, 1000, 100, 100, -1) == 900);
    AGENT_TEST_ASSERT(agent_compact_summary_budget(4096) == 512);
    AGENT_TEST_ASSERT(agent_compact_summary_budget(100000) == 4096);
    AGENT_TEST_ASSERT(agent_compact_summary_budget(1024) == 256);
    ds4_vision_span spans[2] = {
        {.token_start = 100, .embedding = {.token_count = 50}},
        {.token_start = 200, .embedding = {.token_count = 30}},
    };
    for (int glm = 0; glm <= 1; glm++) {
        for (int pos = 0; pos < 260; pos++) {
            int expected = pos;
            for (size_t i = 0; i < 2; i++) {
                int start = (int)spans[i].token_start - glm;
                int end = (int)(spans[i].token_start + spans[i].embedding.token_count) + glm;
                if (pos > start && pos < end) expected = start;
            }
            AGENT_TEST_ASSERT(agent_compact_image_boundary(spans, 2, glm, pos) == expected);
        }
    }
    AGENT_TEST_ASSERT(agent_compact_image_boundary(NULL, 0, false, 77) == 77);
}

static void test_think_commands(void) {
    for (int numeric = 0; numeric <= 1; numeric++) {
        ds4_think_mode mode = DS4_THINK_NONE;
        AGENT_TEST_ASSERT(agent_parse_think("", numeric, &mode) && mode == DS4_THINK_HIGH);
        AGENT_TEST_ASSERT(agent_parse_think("off", numeric, &mode) && mode == DS4_THINK_NONE);
        AGENT_TEST_ASSERT(agent_parse_think("on", numeric, &mode) && mode == DS4_THINK_HIGH);
        AGENT_TEST_ASSERT(!agent_parse_think("invalid", numeric, &mode));
        AGENT_TEST_ASSERT(!agent_parse_think("101", numeric, &mode));
        AGENT_TEST_ASSERT(agent_parse_think("0", numeric, &mode) == (bool)numeric);
        if (numeric) AGENT_TEST_ASSERT(!ds4_think_mode_enabled(mode));
        AGENT_TEST_ASSERT(agent_parse_think("75", numeric, &mode) == (bool)numeric);
        if (numeric) AGENT_TEST_ASSERT(ds4_think_mode_level(mode) == 75);
    }
    AGENT_TEST_ASSERT(agent_slash_command_known("/think off"));
    AGENT_TEST_ASSERT(agent_slash_command_known("/think on"));
    AGENT_TEST_ASSERT(agent_slash_command_known("/nothink"));
    AGENT_TEST_ASSERT(!agent_slash_command_known("/nothinking"));
    AGENT_TEST_ASSERT(!agent_slash_command_known("/nothink extra"));
}

static int test_flash_thinking(const char *model) {
    ds4_engine_options opt = {.model_path = model, .backend = DS4_BACKEND_METAL,
        .context_size = 512, .power_percent = 100};
    agent_config cfg = {.gen = {.ctx_size = 512, .think_mode = DS4_THINK_HIGH}};
    agent_worker w = {.cfg = &cfg, .initialized = true,
        .wake_fd = {-1, -1}, .status = {.state = AGENT_WORKER_IDLE}};
    pthread_mutex_init(&w.mu, NULL);
    AGENT_TEST_ASSERT(ds4_engine_open(&w.engine, &opt) == 0);
    if (!w.engine) return 1;
    AGENT_TEST_ASSERT(!ds4_engine_is_deepseek41(w.engine) &&
                      !ds4_engine_is_glm_dsa(w.engine) && !ds4_engine_is_qwen4(w.engine));
    AGENT_TEST_ASSERT(ds4_session_create(&w.session, w.engine, 512) == 0);
    if (!w.session) { ds4_engine_close(w.engine); return 1; }

    agent_think_prefix(w.engine, DS4_THINK_HIGH, &w.transcript);
    const int prefix_len = w.transcript.len;
    ds4_tokens suffix = {0};
    ds4_tokenize_text(w.engine, "System text.\n\n", &suffix);
    ds4_chat_append_message(w.engine, &suffix, "user", "Remember the code: 4829.");
    ds4_chat_append_message(w.engine, &suffix, "assistant", "The code is 4829.");
    for (int i = 0; i < suffix.len; i++) ds4_tokens_push(&w.transcript, suffix.v[i]);
    ds4_tokens original = {0};
    ds4_tokens_copy(&original, &w.transcript);
    char err[160] = {0};
    AGENT_TEST_ASSERT(ds4_session_sync(w.session, &w.transcript, err, sizeof(err)) == 0);
    const int cached = ds4_session_pos(w.session);
    const char *commands[] = {"off", "on", "off", ""};
    for (size_t i = 0; i < sizeof(commands) / sizeof(*commands); i++) {
        AGENT_TEST_ASSERT(agent_parse_think(commands[i], false, &w.requested_think));
        w.think_requested = true;
        AGENT_TEST_ASSERT(!worker_is_idle(&w) && !worker_submit(&w, "must wait"));
        worker_apply_requested_think(&w);
        AGENT_TEST_ASSERT(worker_is_idle(&w));
        AGENT_TEST_ASSERT(cfg.gen.think_mode == w.requested_think);
        AGENT_TEST_ASSERT(w.transcript.len == original.len &&
                          ds4_tokens_starts_with(&w.transcript, &original));
        AGENT_TEST_ASSERT(ds4_session_pos(w.session) == cached &&
                          ds4_session_common_prefix(w.session, &w.transcript) == cached);
        ds4_tokens next = {0};
        ds4_chat_append_assistant_prefix(w.engine, &next, effective_think_mode(&cfg));
        int marker = agent_special_token_id(w.engine,
            ds4_think_mode_enabled(w.requested_think) ? "<think>" : "</think>");
        AGENT_TEST_ASSERT(next.len > 0 && next.v[next.len - 1] == marker);
        ds4_tokens_free(&next);
    }
    /* A restored max-effort session must lose its instruction when disabled. */
    ds4_tokens_free(&w.transcript);
    agent_think_prefix(w.engine, DS4_THINK_MAX, &w.transcript);
    for (int i = 0; i < suffix.len; i++) ds4_tokens_push(&w.transcript, suffix.v[i]);
    AGENT_TEST_ASSERT(w.transcript.len > original.len);
    AGENT_TEST_ASSERT(ds4_session_sync(w.session, &w.transcript, err, sizeof(err)) == 0);
    w.requested_think = DS4_THINK_NONE;
    w.think_requested = true;
    worker_apply_requested_think(&w);
    AGENT_TEST_ASSERT(cfg.gen.think_mode == DS4_THINK_NONE && w.session_dirty);
    AGENT_TEST_ASSERT(ds4_session_pos(w.session) == 0);
    AGENT_TEST_ASSERT(w.transcript.len == prefix_len + suffix.len &&
                      ds4_tokens_starts_with(&w.transcript, &original));
    while (w.transcript.len < cfg.gen.ctx_size)
        ds4_tokens_push(&w.transcript, ds4_token_eos(w.engine));
    w.requested_think = DS4_THINK_HIGH;
    worker_apply_requested_think(&w);
    AGENT_TEST_ASSERT(cfg.gen.think_mode == DS4_THINK_HIGH &&
                      w.transcript.len == cfg.gen.ctx_size);
    cfg.gen.raw_prompt = true;
    w.requested_think = DS4_THINK_NONE;
    worker_apply_requested_think(&w);
    AGENT_TEST_ASSERT(cfg.gen.think_mode == DS4_THINK_HIGH);
    AGENT_TEST_ASSERT(strstr(w.out, "requires a DeepSeek chat session"));

    free(w.out);
    ds4_tokens_free(&original); ds4_tokens_free(&suffix); ds4_tokens_free(&w.transcript);
    ds4_session_free(w.session); ds4_engine_close(w.engine);
    pthread_mutex_destroy(&w.mu);
    puts("Flash agent thinking toggle, conversation and KV preservation: done");
    return agent_test_failures ? 1 : 0;
}

static int test_v41_thinking(const char *model) {
    ds4_engine_options opt = {.model_path = model, .backend = DS4_BACKEND_METAL,
        .ssd_streaming = true, .ssd_streaming_cache_experts = 512,
        .context_size = 256, .power_percent = 100};
    agent_config cfg = {.gen = {.ctx_size = 256, .think_mode = DS4_THINK_HIGH}};
    agent_worker w = {.cfg = &cfg, .initialized = true,
        .wake_fd = {-1, -1}, .status = {.state = AGENT_WORKER_IDLE}};
    pthread_mutex_init(&w.mu, NULL);
    AGENT_TEST_ASSERT(ds4_engine_open(&w.engine, &opt) == 0);
    if (!w.engine) return 1;
    AGENT_TEST_ASSERT(ds4_session_create(&w.session, w.engine, 256) == 0);
    if (!w.session) { ds4_engine_close(w.engine); return 1; }
    /* Simulate a restored session whose effort differs from the CLI default. */
    agent_think_prefix(w.engine, DS4_THINK_MAX, &w.transcript);
    ds4_tokens suffix = {0};
    ds4_tokenize_text(w.engine, "System text.\n\n", &suffix);
    ds4_chat_append_message(w.engine, &suffix, "user", "Hello");
    ds4_chat_append_message(w.engine, &suffix, "assistant", "Hello again.");
    const int first_image_offset = w.transcript.len;
    for (int i = 0; i < suffix.len; i++) ds4_tokens_push(&w.transcript, suffix.v[i]);
    w.images = calloc(1, sizeof(*w.images));
    w.image_count = 1;
    w.images[0].token_start = (uint32_t)first_image_offset;
    const int levels[] = {25, 0, 100, 1, 75, 75, 0};
    for (size_t i = 0; i < sizeof(levels) / sizeof(*levels); i++) {
        char err[160] = {0};
        AGENT_TEST_ASSERT(ds4_session_sync(w.session, &w.transcript, err, sizeof(err)) == 0);
        const int before = w.transcript.len;
        w.requested_think = (ds4_think_mode)(DS4_THINK_LEVEL_BASE + levels[i]);
        const bool changed = effective_think_mode(&cfg) != w.requested_think;
        w.think_requested = true;
        AGENT_TEST_ASSERT(!worker_is_idle(&w));
        AGENT_TEST_ASSERT(!worker_submit(&w, "must wait"));
        worker_apply_requested_think(&w);
        AGENT_TEST_ASSERT(worker_is_idle(&w));
        AGENT_TEST_ASSERT(ds4_session_pos(w.session) == (changed ? 0 : before));
        ds4_tokens expected = {0};
        agent_think_prefix(w.engine, w.requested_think, &expected);
        AGENT_TEST_ASSERT(w.images[0].token_start == (uint32_t)expected.len);
        for (int j = 0; j < suffix.len; j++) ds4_tokens_push(&expected, suffix.v[j]);
        AGENT_TEST_ASSERT(w.transcript.len == expected.len &&
                           ds4_tokens_starts_with(&w.transcript, &expected));
        ds4_tokens_free(&expected);
    }
    cfg.gen.raw_prompt = true;
    w.requested_think = DS4_THINK_MAX;
    worker_apply_requested_think(&w);
    AGENT_TEST_ASSERT(ds4_think_mode_level(cfg.gen.think_mode) == 0);
    AGENT_TEST_ASSERT(strstr(w.out, "requires a DeepSeek chat session"));
    cfg.gen.raw_prompt = false;
    while (w.transcript.len < 250) ds4_tokens_push(&w.transcript, ds4_token_eos(w.engine));
    worker_apply_requested_think(&w);
    AGENT_TEST_ASSERT(ds4_think_mode_level(cfg.gen.think_mode) == 0 && w.transcript.len == 250);
    AGENT_TEST_ASSERT(strstr(w.out, "no context room"));
    free(w.out); free(w.images);
    ds4_tokens_free(&suffix); ds4_tokens_free(&w.transcript);
    ds4_session_free(w.session); ds4_engine_close(w.engine);
    pthread_mutex_destroy(&w.mu);
    puts("V4.1 agent thinking levels, restored prefix, cache invalidation: done");
    return agent_test_failures ? 1 : 0;
}

static void test_qwen_tool_syntax(void) {
    const char text[] =
        "<think>Plan.</think>\n<tool_call>\n<function=list>\n"
        "<parameter=path>\n.\n</parameter>\n</function>\n</tool_call>";
    for (size_t split = 0; split < sizeof(text); split++) {
        char *first = xstrndup(text, split);
        const char *chunks[] = {first, text + split};
        agent_dsml_parser p;
        char *out = agent_test_stream_capture(AGENT_TOOL_SYNTAX_QWEN, chunks, 2, &p, NULL);
        AGENT_TEST_ASSERT(p.state == AGENT_DSML_DONE && p.calls.len == 1);
        if (p.calls.len == 1) AGENT_TEST_ASSERT(!strcmp(p.calls.v[0].name, "list"));
        free(first);
        free(out);
        agent_dsml_parser_free(&p);
    }
    AGENT_TEST_ASSERT(agent_syntax_is_xml_tool_call(AGENT_TOOL_SYNTAX_QWEN));
    AGENT_TEST_ASSERT(agent_syntax_is_xml_tool_call(AGENT_TOOL_SYNTAX_GLM));
    AGENT_TEST_ASSERT(!agent_syntax_is_xml_tool_call(AGENT_TOOL_SYNTAX_DSML41));
    AGENT_TEST_ASSERT(!agent_syntax_is_xml_tool_call(AGENT_TOOL_SYNTAX_DSML));
}

static void test_v41_tool_syntax(void) {
    const char text[] =
        "<think>Plan.</think>\n\n<｜DSML｜ calls>\n"
        "<｜DSML｜ invoke name=\"write\">\n"
        "<｜DSML｜ parameter name=\"path\" string=\"true\">a.txt</｜DSML｜ parameter>\n"
        "<｜DSML｜ parameter name=\"content\" string=\"true\">x </think> "
        "&lt;/｜DSML｜ parameter> &amp;lt;/｜DSML｜ parameter></｜DSML｜ parameter>\n"
        "</｜DSML｜ invoke>\n<｜DSML｜ invoke name=\"list\">\n"
        "<｜DSML｜ parameter name=\"path\" string=\"true\">.</｜DSML｜ parameter>\n"
        "</｜DSML｜ invoke>\n</｜DSML｜ calls>";
    const char expected[] = "x </think> </｜DSML｜ parameter> &lt;/｜DSML｜ parameter>";
    for (size_t split = 0; split < sizeof(text); split++) {
        char *first = xstrndup(text, split);
        const char *chunks[] = {first, text + split};
        agent_dsml_parser p;
        char *out = agent_test_stream_capture(AGENT_TOOL_SYNTAX_DSML41, chunks, 2, &p, NULL);
        AGENT_TEST_ASSERT(p.state == AGENT_DSML_DONE && p.calls.len == 2);
        if (p.state != AGENT_DSML_DONE || p.calls.len != 2) {
            fprintf(stderr, "V4.1 split=%zu state=%d calls=%d error=%s raw=%s\n",
                    split, p.state, p.calls.len, p.error, p.raw ? p.raw : "(none)");
            free(first); free(out); agent_dsml_parser_free(&p);
            break;
        }
        if (p.calls.len == 2) {
            AGENT_TEST_ASSERT(!strcmp(p.calls.v[0].name, "write"));
            AGENT_TEST_ASSERT(!strcmp(agent_tool_arg_value(&p.calls.v[0], "path"), "a.txt"));
            AGENT_TEST_ASSERT(!strcmp(agent_tool_arg_value(&p.calls.v[0], "content"), expected));
            AGENT_TEST_ASSERT(!strcmp(p.calls.v[1].name, "list"));
        }
        AGENT_TEST_ASSERT(!strstr(out, "<｜DSML｜ calls>"));
        AGENT_TEST_ASSERT(p.raw && strstr(p.raw, "<｜DSML｜ calls>") == p.raw);
        free(first); free(out); agent_dsml_parser_free(&p);
    }
    const char *inside[] = {"<think><｜DSML｜ calls><｜DSML｜ invoke name=\"list\">"
        "</｜DSML｜ invoke></｜DSML｜ calls></think>Done"};
    agent_dsml_parser p;
    bool early = false;
    char *out = agent_test_stream_capture(AGENT_TOOL_SYNTAX_DSML41, inside, 1, &p, &early);
    AGENT_TEST_ASSERT(early && p.calls.len == 0 && strstr(out, "tool call ignored"));
    free(out); agent_dsml_parser_free(&p);
    for (int upto = 0; upto < 2; upto++) {
        char *old = agent_build_dsml_tools_prompt(upto, false);
        char *prompt = agent_dsml41_tools_prompt(old);
        AGENT_TEST_ASSERT(strstr(prompt, "<｜DSML｜ calls>"));
        AGENT_TEST_ASSERT(strstr(prompt, "&amp;lt;/｜DSML｜ parameter>"));
        AGENT_TEST_ASSERT(!strstr(prompt, "｜DSML｜tool_calls") &&
                           !strstr(prompt, "｜DSML｜invoke") &&
                           !strstr(prompt, "｜DSML｜parameter"));
        free(old); free(prompt);
    }
}

/* Model-backed regression: save an unsynchronizable transcript, then rebuild
 * it in a larger context using the normal stripped-session loader. */
static int test_full_context_save(const char *model) {
    ds4_engine_options opt = {.model_path = model, .backend = default_backend(),
        .context_size = 512, .power_percent = 100};
    agent_config cfg = {.gen = {.ctx_size = 256}, .non_interactive = true};
    agent_worker w = {.cfg = &cfg, .initialized = true, .user_activity = true,
        .wake_fd = {-1, -1}, .status = {.state = AGENT_WORKER_IDLE}};
    pthread_mutex_init(&w.mu, NULL);
    char dir[] = "/tmp/ds4-agent-full-save-XXXXXX";
    AGENT_TEST_ASSERT(mkdtemp(dir) != NULL);
    w.cache_dir = dir;
    w.session_title = xstrdup("Full transcript recovery");
    w.session_created_at = 123456;
    AGENT_TEST_ASSERT(ds4_engine_open(&w.engine, &opt) == 0);
    if (!w.engine) return 1;
    ds4_tokens complete = {0};
    ds4_chat_begin(w.engine, &complete);
    for (int i = 0; i < 60; i++)
        ds4_chat_append_message(w.engine, &complete, "user", "Explain the colors of a rainbow.");
    AGENT_TEST_ASSERT(complete.len > 257);
    for (int length = 256; length <= 257; length++) {
        AGENT_TEST_ASSERT(ds4_session_create(&w.session, w.engine, 256) == 0);
        if (!w.session) break;
        ds4_tokens slice = complete;
        slice.len = length;
        ds4_tokens_copy(&w.transcript, &slice);
        w.session_dirty = true;
        char err[256] = {0}, sha[41];
        int saved_tokens = 0;
        AGENT_TEST_ASSERT(agent_worker_save_session_now(&w, sha, &saved_tokens, err, sizeof(err)));
        AGENT_TEST_ASSERT(saved_tokens == length && !w.session_dirty);
        AGENT_TEST_ASSERT(ds4_session_pos(w.session) == 0);
        char *path = agent_kv_path_for_sha(dir, sha);
        FILE *fp = fopen(path, "rb");
        AGENT_TEST_ASSERT(fp != NULL);
        if (!fp) { free(path); ds4_session_free(w.session); w.session = NULL; break; }
        ds4_kvstore_entry hdr = {0};
        uint32_t text_bytes = 0;
        AGENT_TEST_ASSERT(ds4_kvstore_read_header(fp, &hdr, &text_bytes));
        AGENT_TEST_ASSERT(hdr.payload_bytes == 0 && hdr.tokens == (uint32_t)length);
        char *text = NULL, *title = NULL;
        AGENT_TEST_ASSERT(agent_kv_read_text(fp, text_bytes, &text, err, sizeof(err)));
        AGENT_TEST_ASSERT(agent_kv_read_title_trailer(fp, &hdr, &title, err, sizeof(err)));
        AGENT_TEST_ASSERT(title && !strcmp(title, w.session_title));
        fclose(fp);
        size_t expected_len = 0;
        char *expected = ds4_kvstore_render_tokens_text(w.engine, &w.transcript, &expected_len);
        AGENT_TEST_ASSERT(text && expected && text_bytes == expected_len && !strcmp(text, expected));
        free(expected); free(text); free(title);
        ds4_session_free(w.session);
        w.session = NULL;
        AGENT_TEST_ASSERT(ds4_session_create(&w.session, w.engine, 512) == 0);
        cfg.gen.ctx_size = 512;
        ds4_tokens restored = {0};
        agent_kv_session_meta meta = {0};
        AGENT_TEST_ASSERT(agent_kv_load_path(&w, path, sha, NULL, 0, &restored, &meta, err, sizeof(err)));
        AGENT_TEST_ASSERT(agent_tokens_equal(&w.transcript, &restored));
        AGENT_TEST_ASSERT(meta.created_at == w.session_created_at && meta.title && !strcmp(meta.title, w.session_title));
        ds4_token_score score;
        AGENT_TEST_ASSERT(ds4_session_top_logprobs(w.session, &score, 1) == 1);
        agent_kv_session_meta_free(&meta);
        ds4_tokens_free(&restored);
        ds4_tokens_free(&w.transcript);
        ds4_session_free(w.session); w.session = NULL;
        cfg.gen.ctx_size = 256;
        unlink(path); free(path);
    }
    ds4_tokens_free(&complete);
    ds4_engine_close(w.engine);
    free(w.session_title);
    pthread_mutex_destroy(&w.mu);
    rmdir(dir);
    return agent_test_failures ? 1 : 0;
}

static void test_saved_image_data(void) {
    char path[] = "tests/.agent-image-data-XXXXXX";
    int fd = mkstemp(path);
    AGENT_TEST_ASSERT(fd >= 0);
    if (fd < 0) return;
    FILE *fp = fdopen(fd, "w+b");
    AGENT_TEST_ASSERT(fp != NULL);
    if (!fp) { close(fd); unlink(path); return; }
    int ids[] = {1, 10, 11, 2, 3, 12, 13, 4};
    ds4_tokens tokens = {.v = ids, .len = 8, .cap = 8};
    float pixels[][6] = {{1.25f, -2.5f, 3, 4, 5, 6}, {7, 8, 9, 10, 11, 12}};
    ds4_vision_span spans[2] = {
        {.token_start = 1, .embedding = {.token_count = 2, .data = pixels[0],
            .width = 12, .height = 34, .content_width = 10, .content_height = 30,
            .layout = 7, .grid_width = 2, .grid_height = 1, .fingerprint = {42}}},
        {.token_start = 5, .embedding = {.token_count = 2, .data = pixels[1],
            .width = 56, .height = 78, .fingerprint = {43}}},
    };
    char err[256] = {0};
    AGENT_TEST_ASSERT(agent_kv_write_title_trailer(fp, "image fixture", err, sizeof(err)));
    off_t trailer = ftello(fp);
    AGENT_TEST_ASSERT(agent_kv_write_images(fp, &tokens, spans, 2, 3));
    off_t size = ftello(fp);
    ds4_kvstore_entry hdr = {.tokens = 8,
        .ext_flags = DS4_KVSTORE_EXT_SESSION_TITLE | DS4_KVSTORE_EXT_SESSION_IMAGES};
    rewind(fp);
    agent_saved_images restored = {0};
    AGENT_TEST_ASSERT(agent_kv_read_images(fp, &hdr, 3, &restored, err, sizeof(err)));
    AGENT_TEST_ASSERT(ftello(fp) == 0);
    AGENT_TEST_ASSERT(agent_tokens_equal(&tokens, &restored.tokens) && restored.count == 2);
    if (restored.count == 2) {
        for (size_t i = 0; i < 2; i++) {
            AGENT_TEST_ASSERT(restored.images[i].token_start == spans[i].token_start);
            AGENT_TEST_ASSERT(!memcmp(restored.images[i].embedding.data, pixels[i], sizeof(pixels[i])));
            AGENT_TEST_ASSERT(!memcmp(restored.images[i].embedding.fingerprint, spans[i].embedding.fingerprint, 32));
            AGENT_TEST_ASSERT(restored.images[i].embedding.width == spans[i].embedding.width);
        }
        AGENT_TEST_ASSERT(restored.images[0].embedding.content_height == 30);
        AGENT_TEST_ASSERT(restored.images[0].embedding.layout == 7);
        AGENT_TEST_ASSERT(restored.images[0].embedding.grid_width == 2);
    }
    agent_saved_images_free(&restored);
    AGENT_TEST_ASSERT(!agent_kv_read_images(fp, &hdr, 4, &restored, err, sizeof(err)));
    unsigned char *bytes = xmalloc((size_t)size);
    rewind(fp);
    AGENT_TEST_ASSERT(fread(bytes, 1, (size_t)size, fp) == (size_t)size);
    fclose(fp);
    /* Every truncated prefix must fail without publishing partially read images. */
    for (off_t n = 0; n < size; n++) {
        fp = fopen(path, "w+b");
        AGENT_TEST_ASSERT(fp != NULL);
        if (!fp) break;
        AGENT_TEST_ASSERT(fwrite(bytes, 1, (size_t)n, fp) == (size_t)n);
        rewind(fp);
        AGENT_TEST_ASSERT(!agent_kv_read_images(fp, &hdr, 3, &restored, err, sizeof(err)));
        AGENT_TEST_ASSERT(!restored.images && !restored.tokens.v && !restored.count);
        fclose(fp);
    }
    struct { off_t offset; uint32_t value; } invalid[] = {
        {0, 0}, {4, UINT32_MAX}, {8, UINT32_MAX},
        {40, 8}, {44, UINT32_MAX}, {40 + 92, 1},
        {40 + 68, 0x7f800000u},
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        fp = fopen(path, "w+b");
        AGENT_TEST_ASSERT(fp != NULL);
        if (!fp) break;
        AGENT_TEST_ASSERT(fwrite(bytes, 1, (size_t)size, fp) == (size_t)size);
        AGENT_TEST_ASSERT(fseeko(fp, trailer + invalid[i].offset, SEEK_SET) == 0);
        AGENT_TEST_ASSERT(agent_kv_write_words(fp, &invalid[i].value, 1));
        rewind(fp);
        AGENT_TEST_ASSERT(!agent_kv_read_images(fp, &hdr, 3, &restored, err, sizeof(err)));
        AGENT_TEST_ASSERT(!restored.images && !restored.tokens.v && !restored.count);
        fclose(fp);
    }
    free(bytes);
    unlink(path);
}

static int test_image_session_save(const char *model, const char *vision) {
    ds4_engine_options opt = {.model_path = model, .vision_path = vision,
        .backend = default_backend(), .context_size = 4096, .power_percent = 100};
    agent_config cfg = {.gen = {.ctx_size = 4096}, .non_interactive = true};
    agent_worker w = {.cfg = &cfg, .initialized = true, .user_activity = true,
        .wake_fd = {-1, -1}, .status = {.state = AGENT_WORKER_IDLE}};
    pthread_mutex_init(&w.mu, NULL);
    char dir[] = "tests/.agent-image-session-XXXXXX";
    AGENT_TEST_ASSERT(mkdtemp(dir) != NULL);
    w.cache_dir = dir;
    AGENT_TEST_ASSERT(ds4_engine_open(&w.engine, &opt) == 0);
    if (!w.engine) return 1;
    AGENT_TEST_ASSERT(ds4_session_create(&w.session, w.engine, 4096) == 0);
    if (!w.session) { ds4_engine_close(w.engine); return 1; }
    char err[256] = {0}, image_sha[41], text_sha[41];
    ds4_chat_begin(w.engine, &w.transcript);
    ds4_chat_append_message(w.engine, &w.transcript, "user", "Remember the word cedar.");
    w.session_title = xstrdup("text session");
    w.session_created_at = 123;
    AGENT_TEST_ASSERT(agent_worker_save_session_now(&w, text_sha, NULL, err, sizeof(err)));

    const char *fixtures[] = {"tests/vision-fixtures/qwen38/maple.png",
                              "tests/vision-fixtures/qwen38/orbit.png"};
    for (size_t i = 0; i < 2; i++) {
        char *encoded = NULL;
        size_t bytes = 0;
        AGENT_TEST_ASSERT(agent_read_file_bytes(fixtures[i], &encoded, &bytes, err, sizeof(err)) == 0);
        char *path = ds4_kvstore_path_join(dir, "input.png");
        FILE *fp = fopen(path, "wb");
        AGENT_TEST_ASSERT(fp != NULL);
        if (!fp) { free(path); free(encoded); continue; }
        AGENT_TEST_ASSERT(fwrite(encoded, 1, bytes, fp) == bytes);
        fclose(fp);
        free(encoded);
        ds4_vision_embedding embedding = {0};
        AGENT_TEST_ASSERT(ds4_engine_vision_encode_file(w.engine, path, &embedding, err, sizeof(err)));
        unlink(path);
        free(path);
        const char *parts[] = {"Read this image.", ""};
        ds4_vision_span span = {0};
        AGENT_TEST_ASSERT(ds4_chat_append_multimodal_message(w.engine, &w.transcript,
            "user", parts, &embedding, 1, &span, err, sizeof(err)));
        agent_worker_images_append(&w, &span, 1);
    }
    ds4_chat_append_assistant_prefix(w.engine, &w.transcript, DS4_THINK_NONE);
    ds4_session_invalidate(w.session);
    AGENT_TEST_ASSERT(agent_worker_sync_tokens(&w, &w.transcript, false, err, sizeof(err)) == 0);
    ds4_tokens original = {0};
    ds4_tokens_copy(&original, &w.transcript);
    ds4_token_score expected[8], actual[8];
    AGENT_TEST_ASSERT(ds4_session_top_logprobs(w.session, expected, 8) == 8);
    free(w.session_title);
    w.session_title = xstrdup("two image session");
    w.session_created_at = 456;
    AGENT_TEST_ASSERT(agent_worker_save_session_now(&w, image_sha, NULL, err, sizeof(err)));
    AGENT_TEST_ASSERT(!w.session_dirty && w.image_count == 2);
    /* Preserve an image transcript even when a smaller context cannot prefill it. */
    ds4_session_free(w.session);
    w.session = NULL;
    AGENT_TEST_ASSERT(ds4_session_create(&w.session, w.engine, 256) == 0);
    cfg.gen.ctx_size = 256;
    AGENT_TEST_ASSERT(agent_worker_save_session_now(&w, image_sha, NULL, err, sizeof(err)));
    cfg.gen.ctx_size = 4096;
    uint32_t stripped_tokens = 0;
    AGENT_TEST_ASSERT(agent_worker_strip_session(&w, image_sha, NULL, &stripped_tokens, err, sizeof(err)));
    AGENT_TEST_ASSERT(stripped_tokens == (uint32_t)original.len);

    /* A new backend session ensures the restore cannot reuse the live image KV. */
    ds4_session_free(w.session);
    w.session = NULL;
    agent_worker_images_clear(&w);
    ds4_tokens_free(&w.transcript);
    AGENT_TEST_ASSERT(ds4_session_create(&w.session, w.engine, 4096) == 0);
    w.status.state = AGENT_WORKER_IDLE;
    bool ok = agent_worker_switch_session(&w, image_sha, 0, err, sizeof(err));
    if (!ok) fprintf(stderr, "image restore: %s\n", err);
    AGENT_TEST_ASSERT(ok);
    AGENT_TEST_ASSERT(w.image_count == 2 && agent_tokens_equal(&original, &w.transcript));
    AGENT_TEST_ASSERT(ds4_session_vision_state_matches(w.session, w.images, w.image_count));
    AGENT_TEST_ASSERT(ds4_session_top_logprobs(w.session, actual, 8) == 8);
    for (int i = 0; i < 8; i++) {
        AGENT_TEST_ASSERT(actual[i].id == expected[i].id);
        AGENT_TEST_ASSERT(fabsf(actual[i].logit - expected[i].logit) < 0.02f);
    }
    int next = ds4_session_argmax(w.session);
    AGENT_TEST_ASSERT(ds4_session_eval(w.session, next, err, sizeof(err)) == 0);
    ds4_tokens_push(&w.transcript, next);
    AGENT_TEST_ASSERT(agent_worker_save_session_now(&w, image_sha, NULL, err, sizeof(err)));
    AGENT_TEST_ASSERT(agent_worker_switch_session(&w, text_sha, 0, err, sizeof(err)));
    AGENT_TEST_ASSERT(w.image_count == 0 && !ds4_session_has_vision_state(w.session));
    AGENT_TEST_ASSERT(agent_worker_switch_session(&w, image_sha, 0, err, sizeof(err)));
    AGENT_TEST_ASSERT(w.image_count == 2 && w.transcript.len == original.len + 1);

    char *image_path = agent_kv_path_for_sha(dir, image_sha);
    char *text_path = agent_kv_path_for_sha(dir, text_sha);
    FILE *fp = fopen(image_path, "r+b");
    AGENT_TEST_ASSERT(fp != NULL);
    if (fp) {
        AGENT_TEST_ASSERT(fseeko(fp, 0, SEEK_END) == 0);
        AGENT_TEST_ASSERT(ftruncate(fileno(fp), ftello(fp) - 1) == 0);
        fclose(fp);
        ds4_vision_span *before = w.images;
        int pos = ds4_session_pos(w.session);
        AGENT_TEST_ASSERT(!agent_worker_switch_session(&w, image_sha, 0, err, sizeof(err)));
        AGENT_TEST_ASSERT(w.images == before && w.image_count == 2);
        AGENT_TEST_ASSERT(ds4_session_pos(w.session) == pos && ds4_session_checkpoint_valid(w.session));
    }
    unlink(image_path); unlink(text_path);
    free(image_path); free(text_path);
    ds4_tokens_free(&original);
    ds4_tokens_free(&w.transcript);
    agent_worker_images_clear(&w);
    ds4_session_free(w.session);
    ds4_engine_close(w.engine);
    free(w.session_title);
    free(w.legacy_session_path_to_delete);
    pthread_mutex_destroy(&w.mu);
    rmdir(dir);
    if (!agent_test_failures) puts("image session save/restore: ok");
    return agent_test_failures ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "--image-session-save")) return test_image_session_save(argv[2], argv[3]);
    if (argc == 3 && !strcmp(argv[1], "--flash-think-toggle")) return test_flash_thinking(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--full-context-save")) return test_full_context_save(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--think-fixture")) return test_v41_thinking(argv[2]);
    if (argc == 2 && !strcmp(argv[1], "--terminal-driver")) return test_terminal_driver();
    if (argc == 3 && !strcmp(argv[1], "--terminal-fixtures")) test_output_dir = argv[2];
    char *options[] = {"ds4-agent", "--model", "qwen.gguf",
                    "--vision", "mmproj.gguf", "--non-interactive", "-p", "test"};
    agent_config cfg = parse_options((int)(sizeof(options) / sizeof(options[0])), options);
    AGENT_TEST_ASSERT(cfg.engine.vision_path && !strcmp(cfg.engine.vision_path, "mmproj.gguf"));
    AGENT_TEST_ASSERT(cfg.engine.model_path && !strcmp(cfg.engine.model_path, "qwen.gguf"));
    test_agent_cli();
    test_agent_runtime();
    test_think_commands();
    test_saved_image_data();
    ds4_agent_unit_tests_run();
    test_v41_tool_syntax();
    test_qwen_tool_syntax();
    test_compaction_boundaries();
    test_observation_error_is_not_context_exhaustion();
    test_atomic_file_tools();
    test_streaming_file_tools();
    test_shell_spawn();
    test_background_jobs();
    test_fragmented_terminal_input();
    test_shell_terminal_controls();
    test_markdown_literals();
    test_hint_rendering();
    test_unicode_output_and_footer();
    test_footer_only_updates();
    test_tool_contracts();
    if (agent_test_failures) {
        fprintf(stderr, "ds4-agent tests: %d failure(s)\n",
                agent_test_failures);
        return 1;
    }
    puts("ds4-agent tests: ok");
    return 0;
}
