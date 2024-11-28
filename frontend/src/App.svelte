<script lang="ts">
  import { onMount } from "svelte";

  type Service = {
    unit: string;
    sub: string;
    description: string;
  };

  let services: Service[] = [];
  let filterText = "";

  async function fetchServices() {
    const response = await fetch("http://localhost:8080/api/services");
    services = await response.json();
  }

  async function toggleService(unit: string, shouldStart: boolean) {
    await fetch(`http://localhost:8080/api/services/${unit}/toggle`, {
      method: "POST",
      body: JSON.stringify({ start: shouldStart }),
    });
    await fetchServices();
  }

  onMount(fetchServices);
</script>

<main class="container mx-auto p-4">
  <h1 class="text-3xl font-bold mb-4">g-systemctl</h1>
  <p class="text-gray-500 mb-4">
    A simple service manager for macOS and Linux.
  </p>

  <input
    type="text"
    bind:value={filterText}
    placeholder="Filter services..."
    class="w-full p-2 mb-4 border rounded"
  />

  <div class="grid gap-4 sm:grid-cols-1 md:grid-cols-2">
    {#each services.filter((s) => s.unit
        .toLowerCase()
        .includes(filterText.toLowerCase())) as service}
      <div
        class="p-4 border rounded shadow-md {service.sub === 'running'
          ? 'bg-green-50 border-green-200'
          : 'bg-gray-50 border-gray-200'}"
      >
        <div class="flex justify-between items-center mb-2">
          <h2 class="text-lg font-semibold truncate">{service.unit}</h2>
          <button
            on:click={() =>
              toggleService(service.unit, service.sub !== "running")}
            class="px-4 py-2 text-sm font-bold rounded shadow {service.sub ===
            'running'
              ? 'bg-red-500 text-white hover:bg-red-600'
              : 'bg-green-500 text-white hover:bg-green-600'}"
          >
            {service.sub === "running" ? "STOP" : "START"}
          </button>
        </div>
        <p class="text-sm text-gray-600">
          Status: <span
            class={service.sub === "running"
              ? "text-green-600"
              : "text-gray-500"}>{service.sub}</span
          >
        </p>
        {#if service.description}
          <p class="mt-2 text-sm text-gray-500">
            {service.description}
          </p>
        {/if}
      </div>
    {/each}
  </div>
</main>
