/* microWakeWord as a plugin of hassmic's (its docs/plugins.md 4.13): the wake word engine of ESPHome's voice
 * satellites, in a process of its own.  The engine came out of hassmic (mww_features.c: TFLite Micro's audio
 * front end; mww_model.c: an int8 interpreter for the streaming models microWakeWord trains), put together here as
 * hassmic's wake_mww.c puts it together inside the daemon: features every 10 ms, an inference every stride (3) of
 * them, and a detection when the mean probability over the model's window passes its cutoff, as ESPHome decides it.
 *
 * What it gains by being out here: the model is a file the owner uploads, and its parser now reads it in a process
 * that has no file, no socket and none of hassmic's keys.  A model that gets past the checks breaks the wake word
 * and nothing else.
 *
 * Models are the items of the file list "models": model.tflite and, if it came with one, microWakeWord's
 * manifest.json (the wake word's name, its cutoff and window).  The owner's fields win over the manifest's values.
 * The stream is hassmic's micAsr as the engine in hassmic gets it, without gain: the front end normalises the level
 * (PCAN).  A block lost (the plugin fell behind) or a mute shows as a jump of the position: the front end starts
 * anew, as on wake_reset. */
#include "hassmic_plugin.h"
#include "mww.h"
#include "json.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 16000
#define LIST "models"
#define HOLD_INFERENCES 33              /* ~1 s after opening and after a detection: no detection (ESPHome waits too) */
#define CUTOFF_DEFAULT 0.97f            /* a model without a manifest: ESPHome's own are 0.85 to 0.97 */
#define WINDOW_DEFAULT 5

static struct { char id[41]; float cutoff; int window; } tuned[16];     /* the owner's fields, per item */
static int ntuned;
static struct slot {
    struct mww_features fe; int fe_ok;
    struct mww_model *model;
    char word[41];
    int stride, window, threshold, have, nprob, hold;
    uint8_t prob[MWW_WINDOW_MAX];
    uint16_t feat[16][MWW_CHANNELS];
} slots[2];
static uint64_t expect;                 /* the next block's position, 0 before the first */

/* microWakeWord's manifest: { "type": "micro", "wake_word": ..., "micro": { "probability_cutoff", "sliding_window_size",
 * "feature_step_size" } }.  0, or -1 with why */
static int manifest(const char *text, size_t len, char *name, size_t ncap, float *cutoff, int *window, char *why, size_t wcap)
{
    struct json j = { 0 }; char t[64]; int r = -1, micro;
    if (json_parse(&j, text, len) || json_type(&j, 0) != J_OBJ) { snprintf(why, wcap, "its manifest is not JSON"); goto out; }
    if (json_str(&j, json_get(&j, 0, "type"), t, sizeof t) && strcmp(t, "micro")) { snprintf(why, wcap, "a \"%.20s\" model, not microWakeWord's", t); goto out; }
    if (!json_str(&j, json_get(&j, 0, "wake_word"), name, ncap) || !name[0]) { snprintf(why, wcap, "its manifest names no wake_word"); goto out; }
    micro = json_get(&j, 0, "micro");
    *cutoff = (float)json_num(&j, json_get(&j, micro, "probability_cutoff"), 0);
    if (!(*cutoff > 0 && *cutoff < 1)) { snprintf(why, wcap, "its manifest has no probability_cutoff"); goto out; }
    *window = (int)json_int(&j, json_get(&j, micro, "sliding_window_size"), WINDOW_DEFAULT);
    if (*window < MWW_WINDOW_MIN || *window > MWW_WINDOW_MAX) { snprintf(why, wcap, "its sliding_window_size is %d", *window); goto out; }
    if (json_int(&j, json_get(&j, micro, "feature_step_size"), 10) != 10) { snprintf(why, wcap, "it wants features every %d ms: this engine makes them every 10 ms", (int)json_int(&j, json_get(&j, micro, "feature_step_size"), 10)); goto out; }
    r = 0;
out:
    json_free(&j);
    return r;
}

/* an item's name, cutoff and window as its files have them: the manifest's, or the defaults for a model that came
 * without one, named after its id ("hey_jarvis" -> "Hey Jarvis") */
static int item_info(const char *item, char *name, size_t ncap, float *cutoff, int *window, char *why, size_t wcap)
{
    void *d; size_t n; int r;
    *cutoff = CUTOFF_DEFAULT; *window = WINDOW_DEFAULT;
    snprintf(name, ncap, "%s", item);
    for (char *c = name; *c; c++) { if (*c == '_' || *c == '-') *c = ' '; else if (c == name || c[-1] == ' ') *c = (char)toupper((unsigned char)*c); }
    if (hm_file_read(LIST, item, ".json", 16384, &d, &n)) return 0;     /* optional */
    r = manifest(d, n, name, ncap, cutoff, window, why, wcap);
    free(d);
    return r;
}

static struct mww_model *model_load(const char *item, char *why, size_t wcap)
{
    void *d; size_t n; struct mww_model *m;
    if (hm_file_read(LIST, item, ".tflite", MWW_MODEL_MAX, &d, &n)) { snprintf(why, wcap, "it has no model file (or one over %d kB)", MWW_MODEL_MAX >> 10); return NULL; }
    m = mww_model_load(d, n, why, wcap);
    free(d);
    return m;
}

static void slot_tune(struct slot *s)       /* the owner's fields over the file's values */
{
    char name[64], why[100]; float cutoff; int window;
    if (item_info(s->word, name, sizeof name, &cutoff, &window, why, sizeof why)) { cutoff = CUTOFF_DEFAULT; window = WINDOW_DEFAULT; }
    for (int i = 0; i < ntuned; i++) if (!strcmp(tuned[i].id, s->word)) {
        if (tuned[i].cutoff >= MWW_CUTOFF_MIN && tuned[i].cutoff <= MWW_CUTOFF_MAX) cutoff = tuned[i].cutoff;
        if (tuned[i].window >= MWW_WINDOW_MIN && tuned[i].window <= MWW_WINDOW_MAX) window = tuned[i].window;
    }
    if (window != s->window) s->nprob = 0;
    s->window = window; s->threshold = (int)(cutoff * 255);
    hm_log("\"%s\": cutoff %.2f over %d x %d ms", s->word, cutoff, window, s->stride * 10);
}

static void on_files(void *ctx, const char *list, const struct hm_item *items, int n)
{
    const char *ids[16]; int k = 0;
    (void)ctx;
    if (strcmp(list, LIST)) return;
    ntuned = 0;
    for (int i = 0; i < n && k < 16; i++) {
        if (!items[i].checked) continue;                    /* not looked at yet: file_check comes for it */
        ids[k++] = items[i].id;
        snprintf(tuned[ntuned].id, sizeof tuned[0].id, "%s", items[i].id); tuned[ntuned].cutoff = 0; tuned[ntuned].window = 0;
        for (int f = 0; f < items[i].nfields; f++) {
            if (!strcmp(items[i].fields[f].name, "cutoff")) tuned[ntuned].cutoff = (float)atof(items[i].fields[f].value);
            if (!strcmp(items[i].fields[f].name, "window")) tuned[ntuned].window = atoi(items[i].fields[f].value);
        }
        ntuned++;
    }
    hm_wake_words(LIST, ids, k);
    for (int s = 0; s < 2; s++) if (slots[s].model) slot_tune(&slots[s]);     /* a field the owner changed: at once */
}

/* an upload: it must load in this interpreter, as it would have to before it is listened for */
static int on_check(void *ctx, const char *list, const char *item, struct hm_check *out)
{
    struct mww_model *m; float cutoff; int window;
    (void)ctx;
    if (strcmp(list, LIST)) { snprintf(out->why, sizeof out->why, "no such list"); return HM_FILE_BAD_OTHER; }
    if (!(m = model_load(item, out->why, sizeof out->why))) return HM_FILE_BAD_FORMAT;
    mww_model_free(m);
    if (item_info(item, out->title, sizeof out->title, &cutoff, &window, out->why, sizeof out->why)) return HM_FILE_BAD_UNSUPPORTED;
    snprintf(out->fields[0].name, sizeof out->fields[0].name, "cutoff"); snprintf(out->fields[0].value, sizeof out->fields[0].value, "%.2f", cutoff);
    snprintf(out->fields[1].name, sizeof out->fields[1].name, "window"); snprintf(out->fields[1].value, sizeof out->fields[1].value, "%d", window);
    out->nfields = 2;
    return 0;
}

static void slot_close(struct slot *s)
{
    mww_model_free(s->model); s->model = NULL;
    if (s->fe_ok) { mww_features_free(&s->fe); s->fe_ok = 0; }
}

static int on_open(void *ctx, int n, const char *word)
{
    struct slot *s; char why[200];
    (void)ctx;
    if (n < 0 || n > 1) return HM_WAKE_FAIL_OTHER;
    s = &slots[n];
    slot_close(s);
    if (!(s->model = model_load(word, why, sizeof why))) { hm_log("%s: %s", word, why); return HM_WAKE_FAIL_BAD_MODEL; }
    if (mww_features_init(&s->fe)) { slot_close(s); return HM_WAKE_FAIL_MEMORY; }
    s->fe_ok = 1;
    snprintf(s->word, sizeof s->word, "%s", word);
    s->stride = mww_model_stride(s->model); s->have = s->nprob = 0; s->hold = HOLD_INFERENCES; s->window = 0;
    if (s->stride < 1 || s->stride > 16) { slot_close(s); return HM_WAKE_FAIL_UNSUPPORTED; }
    slot_tune(s);
    return 0;
}

static void on_close(void *ctx, int n) { (void)ctx; if (n >= 0 && n <= 1) slot_close(&slots[n]); }

static void on_reset(void *ctx)
{
    (void)ctx;
    for (int i = 0; i < 2; i++) { slots[i].nprob = 0; slots[i].hold = HOLD_INFERENCES; }
}

static void on_audio(void *ctx, uint64_t pos, const int16_t *pcm, int count)
{
    (void)ctx;
    if (expect && pos != expect)                            /* blocks lost, or the Echo was muted: nothing of before counts */
        for (int i = 0; i < 2; i++) if (slots[i].model) { mww_features_reset(&slots[i].fe); mww_model_reset(slots[i].model); slots[i].have = slots[i].nprob = 0; slots[i].hold = HOLD_INFERENCES; }
    expect = pos + (uint64_t)count;
    for (int i = 0; i < 2; i++) {
        struct slot *m = &slots[i]; const int16_t *s = pcm; size_t n = (size_t)count; uint64_t fed = pos;
        if (!m->model) continue;
        while (n) {
            int done, p, sum = 0;
            size_t k = mww_features_run(&m->fe, s, n, m->feat[m->have], &done);
            s += k; n -= k; fed += k;
            if (!done || ++m->have < m->stride) continue;
            m->have = 0;
            if ((p = mww_model_run(m->model, m->feat[0])) < 0) { hm_log("the model broke: started anew"); mww_model_reset(m->model); m->nprob = 0; continue; }
            memmove(m->prob + 1, m->prob, sizeof m->prob[0] * (MWW_WINDOW_MAX - 1));
            m->prob[0] = (uint8_t)p;
            if (m->nprob < m->window) m->nprob++;
            if (m->hold) { m->hold--; continue; }
            if (m->nprob < m->window) continue;
            for (int w = 0; w < m->window; w++) sum += m->prob[w];
            if (sum <= m->threshold * m->window) continue;
            /* the window ends a feature window (30 ms) and up to `window` inferences after the word: it began a second before */
            m->nprob = 0; m->hold = HOLD_INFERENCES;
            hm_wake_heard(i, fed > RATE ? fed - RATE : 0, fed, sum / m->window);
        }
    }
}

int main(void)
{
    static const struct hm_callbacks cb = { NULL, on_files, on_check, NULL, on_open, on_close, on_reset, on_audio, NULL, NULL, NULL, NULL };
    return hm_run(&cb, NULL) ? 1 : 0;
}
