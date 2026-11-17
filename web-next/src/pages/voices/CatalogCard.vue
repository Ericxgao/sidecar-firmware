<!-- Voices › User machines › Catalog: the community registry's machines and effects for this board, read by this browser
     (src/lib/catalog.ts; the Pi never goes to the internet). Install / Update downloads the package here, checks its
     SHA-256, and sends it through the ordinary upload into /Plugins; the Pi's loader checks it again. -->
<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import Card from "../../ui/Card.vue";
import { useSidecar } from "../../lib/useSidecar";
import { confirmAsk } from "../../lib/confirm";
import { uploadFile } from "../../lib/upload";
import { CAT_BASE, boardOf, catAsk, catFileName, catShown, catState, fetchPackage, loadIndex, type CatEntry } from "../../lib/catalog";

const props = defineProps<{ installed: { [k: string]: any }[] }>();
const emit = defineEmits<{ done: [] }>();
const { api, status, paired } = useSidecar();
const entries = ref<CatEntry[]>([]);
const err = ref("");
const loading = ref(false);
const kind = ref("");
const q = ref("");
const msg = ref("");
const bad = ref(false);
const busy = ref("");
const board = computed(() => boardOf((status.value?.usbMode as { board?: number } | undefined)?.board));
const shown = computed(() => catShown(entries.value, kind.value, q.value, board.value));
const total = computed(() => entries.value.filter((e) => !board.value || e.board === board.value).length);
const rows = computed(() => shown.value.map((e) => ({ e, st: catState(e, props.installed) })));

async function load() {
  loading.value = true;
  try {
    entries.value = await loadIndex();
    err.value = "";
  } catch (e) {
    entries.value = [];
    err.value = (e as Error).message;
  }
  loading.value = false;
}
onMounted(load);
async function install(e: CatEntry, file?: string) {
  if (busy.value) return;
  if (!(await confirmAsk({ title: `${file ? "Replace" : "Install"} ${e.name || e.id}`, body: catAsk(e) + (file ? ` It replaces /Plugins/${file}.` : ""), yes: file ? "Replace" : "Install" }))) return;
  const name = file || catFileName(e); // an update or a reinstall replaces the card's existing file for this id
  busy.value = e.id;
  bad.value = false;
  msg.value = `downloading ${name}`;
  try {
    const buf = await fetchPackage(e);
    msg.value = "checked (SHA-256 matches), sending to the Pi";
    const r = await uploadFile(api, new File([buf], name, { type: "application/octet-stream" }), `/Plugins/${name}`, (p) => (msg.value = `${name}: ${p.text}`));
    msg.value = `installed ${name}: ${r}`;
    emit("done");
  } catch (x) {
    bad.value = true;
    msg.value = (x as Error).message;
  }
  busy.value = "";
}
</script>

<template>
  <Card label="Catalog" tone="plain">
    <p v-if="!paired">The catalog needs the pairing (it installs onto the Sidecar).</p>
    <template v-else>
      <div class="row">
        <span class="small" role="status">
          <template v-if="loading">loading…</template>
          <template v-else-if="err">Catalog not reachable</template>
          <template v-else>{{ shown.length }} of {{ total }} in the community registry<template v-if="board"> for {{ board }}</template></template>
        </span>
        <span class="grow"></span>
        <label for="cat-kind" class="visually-hidden">Kind</label>
        <select id="cat-kind" v-model="kind"><option value="">Machines and effects</option><option value="machine">Machines</option><option value="effect">Effects</option></select>
        <label for="cat-q" class="visually-hidden">Search the catalog</label>
        <input id="cat-q" v-model="q" type="search" placeholder="Search" autocomplete="off" />
        <button type="button" :disabled="loading" @click="load">Reload</button>
      </div>
      <p v-if="err" class="small">
        Nothing else is affected. This browser could not read the community registry ({{ err }}); it needs internet in the browser, not on the Sidecar.
        <a :href="CAT_BASE" target="_blank" rel="noopener noreferrer">Open the site</a>
      </p>
      <p v-else-if="!loading && !rows.length" class="small">Nothing matches.</p>
      <ul class="cat">
        <li v-for="{ e, st } in rows" :key="`${e.id}/${e.board}`">
          <div class="h">
            <b>{{ e.name || e.id }}</b>
            <span class="tag">{{ e.short }}</span>
            <span class="tag">{{ e.kind }}</span>
            <span class="tag warm" title="Made by its author, not by Sidecar: native code, no sandbox, not tested by Sidecar">community</span>
            <span v-if="e.author === 'trigcondition'" class="tag">by Sidecar</span>
            <button v-if="st.k === 'install' || st.k === 'update'" type="button" :disabled="!!busy || !board" :title="board ? undefined : 'waiting for the board (Pi 4 or Pi 5)'" @click="install(e, st.file)">{{ st.text }}</button>
            <span v-else class="tag">{{ st.text }}</span>
          </div>
          <div class="small">
            {{ e.id }} · {{ e.version }} · by {{ e.author }} · {{ e.licence }} · {{ Math.round(e.size / 102.4) / 10 }} KB · {{ e.board }}<template v-if="st.from"> · installed {{ st.from }}</template> ·
            <a v-if="e.source" :href="e.source" target="_blank" rel="noopener noreferrer">source</a><template v-else>no source</template>
            <template v-if="e.demo"> · <a :href="e.demo" target="_blank" rel="noopener noreferrer">demo</a></template>
          </div>
          <div class="desc">{{ e.description }}</div>
        </li>
      </ul>
      <p v-if="msg" class="small" :class="{ warm: bad }" role="status">{{ msg }}</p>
      <p class="small">The list comes from the community registry, read by this browser: the Pi itself never goes to the internet. A download is checked against its SHA-256 before it is sent to the Pi, which checks the package again as for any upload.</p>
    </template>
  </Card>
</template>

<style scoped>
.row { display: flex; flex-wrap: wrap; align-items: center; gap: 0.5rem; }
.grow { flex: 1; }
.cat { list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; gap: 0.375rem; }
.cat li { border: 1px solid var(--border); border-radius: var(--radius-control); padding: 0.375rem 0.625rem; display: flex; flex-direction: column; gap: 0.125rem; min-width: 0; }
.h { display: flex; flex-wrap: wrap; align-items: center; gap: 0.375rem 0.5rem; }
.tag { font-size: 0.75rem; padding: 0 0.5rem; border: 1px solid var(--border); border-radius: var(--radius-pill); white-space: nowrap; }
.tag.warm { border-color: var(--warm-border); color: var(--warm); }
.desc { overflow-wrap: anywhere; }
</style>
