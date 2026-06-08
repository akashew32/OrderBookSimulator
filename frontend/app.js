const model = {
  state: null,
  previousTradeId: null,
  previousSequence: null
};

const $ = (id) => document.getElementById(id);

const els = {
  statusDot: $("statusDot"),
  connectionStatus: $("connectionStatus"),
  ordersProcessed: $("ordersProcessed"),
  tradesExecuted: $("tradesExecuted"),
  volumeExecuted: $("volumeExecuted"),
  spread: $("spread"),
  latency: $("latency"),
  throughput: $("throughput"),
  canceledOrders: $("canceledOrders"),
  modifiedOrders: $("modifiedOrders"),
  pendingEvents: $("pendingEvents"),
  droppedEvents: $("droppedEvents"),
  fillDelay: $("fillDelay"),
  missedTrades: $("missedTrades"),
  totalSlippage: $("totalSlippage"),
  runState: $("runState"),
  startBtn: $("startBtn"),
  stopBtn: $("stopBtn"),
  speedInput: $("speedInput"),
  speedValue: $("speedValue"),
  orderLimitInput: $("orderLimitInput"),
  orderLatencyInput: $("orderLatencyInput"),
  marketLatencyInput: $("marketLatencyInput"),
  replayPlayBtn: $("replayPlayBtn"),
  replayStepBtn: $("replayStepBtn"),
  replaySpeedInput: $("replaySpeedInput"),
  replaySpeedValue: $("replaySpeedValue"),
  csvSelect: $("csvSelect"),
  csvUploadInput: $("csvUploadInput"),
  uploadCsvBtn: $("uploadCsvBtn"),
  csvMetadata: $("csvMetadata"),
  loadReplayBtn: $("loadReplayBtn"),
  resetBtn: $("resetBtn"),
  jumpInput: $("jumpInput"),
  jumpBtn: $("jumpBtn"),
  replayStatus: $("replayStatus"),
  feeInput: $("feeInput"),
  slippageInput: $("slippageInput"),
  orderForm: $("orderForm"),
  orderType: $("orderType"),
  priceInput: $("priceInput"),
  quantityInput: $("quantityInput"),
  bestBid: $("bestBid"),
  bestAsk: $("bestAsk"),
  bookRows: $("bookRows"),
  tradeTape: $("tradeTape"),
  tradePrints: $("tradePrints"),
  lastPrice: $("lastPrice"),
  priceChart: $("priceChart"),
  depthChart: $("depthChart"),
  volumeChart: $("volumeChart"),
  strategyRows: $("strategyRows"),
  activeOrders: $("activeOrders"),
  lifecycleEvents: $("lifecycleEvents"),
  processPhase: $("processPhase"),
  threadRoles: $("threadRoles"),
  queueHealth: $("queueHealth"),
  syncMap: $("syncMap"),
  optimizationStrategy: $("optimizationStrategy"),
  rankMetric: $("rankMetric"),
  trainWindow: $("trainWindow"),
  testWindow: $("testWindow"),
  runOptimizationBtn: $("runOptimizationBtn"),
  exportOptimizationBtn: $("exportOptimizationBtn"),
  optimizationStatus: $("optimizationStatus"),
  optimizationRows: $("optimizationRows"),
  bestConfig: $("bestConfig"),
  optimizationChart: $("optimizationChart"),
  parameterHeatmap: $("parameterHeatmap"),
  pnlChart: $("pnlChart"),
  riskChart: $("riskChart")
};

function connect() {
  const stream = new EventSource("/events");

  stream.addEventListener("open", () => {
    els.statusDot.className = "dot connected";
    els.connectionStatus.textContent = "Connected";
  });

  stream.addEventListener("error", () => {
    els.statusDot.className = "dot disconnected";
    els.connectionStatus.textContent = "Reconnecting";
  });

  stream.addEventListener("state", (event) => {
    model.state = JSON.parse(event.data);
    render();
  });
}

async function sendControl(running) {
  await fetch("/api/control", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      running,
      speed: Number(els.speedInput.value),
      orderLimit: Number(els.orderLimitInput.value),
      replayRunning: false,
      replaySpeed: Number(els.replaySpeedInput.value),
      orderSubmissionLatencyMs: Number(els.orderLatencyInput.value),
      marketDataLatencyMs: Number(els.marketLatencyInput.value),
      feePerShare: Number(els.feeInput.value),
      slippagePerShare: Number(els.slippageInput.value)
    })
  });
}

async function sendReplayControl(running) {
  await fetch("/api/control", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      running: false,
      replayRunning: running,
      speed: Number(els.speedInput.value),
      orderLimit: Number(els.orderLimitInput.value),
      replaySpeed: Number(els.replaySpeedInput.value),
      orderSubmissionLatencyMs: Number(els.orderLatencyInput.value),
      marketDataLatencyMs: Number(els.marketLatencyInput.value),
      feePerShare: Number(els.feeInput.value),
      slippagePerShare: Number(els.slippageInput.value)
    })
  });
}

async function loadReplay() {
  const path = els.csvSelect.value || "data/sample_replay.csv";
  const response = await fetch("/api/replay/load", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ path })
  });
  const result = await response.json();
  if (!result.ok) {
    els.replayStatus.textContent = result.error || "Could not load CSV";
  }
}

async function uploadCsv() {
  const file = els.csvUploadInput.files[0];
  if (!file) return;
  const form = new FormData();
  form.append("file", file);
  const response = await fetch("/api/csv/upload", { method: "POST", body: form });
  const result = await response.json();
  if (!result.ok) {
    els.csvMetadata.textContent = result.error || "Upload failed";
    return;
  }
  await refreshCsvFiles(result.file?.path);
}

async function refreshCsvFiles(preferredPath) {
  const response = await fetch("/api/csv/list");
  const result = await response.json();
  if (!result.ok) return;
  renderCsvFiles(result.files || [], preferredPath || model.state?.replay?.source);
}

async function runOptimization() {
  els.optimizationStatus.textContent = "Running";
  const response = await fetch("/api/optimization/run", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      csvPath: els.csvSelect.value,
      strategy: els.optimizationStrategy.value,
      rankMetric: els.rankMetric.value,
      trainWindow: Number(els.trainWindow.value),
      testWindow: Number(els.testWindow.value)
    })
  });
  const result = await response.json();
  if (!result.ok) {
    els.optimizationStatus.textContent = result.error || "Optimization failed";
  }
}

async function stepReplay() {
  await fetch("/api/replay/step", { method: "POST" });
}

async function jumpReplay() {
  await fetch("/api/replay/jump", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ timestamp: Number(els.jumpInput.value) })
  });
}

async function resetBackend() {
  await fetch("/api/reset", { method: "POST" });
}

async function sendManualOrder(event) {
  event.preventDefault();
  const side = new FormData(els.orderForm).get("side");
  const type = els.orderType.value;

  await fetch("/api/order", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      side,
      type,
      price: Number(els.priceInput.value),
      quantity: Number(els.quantityInput.value)
    })
  });

  els.orderForm.classList.add("pulse");
  window.setTimeout(() => els.orderForm.classList.remove("pulse"), 520);
}

function render() {
  const state = model.state;
  if (!state) return;

  renderMetrics(state);
  renderBook(state);
  renderTape(state);
  renderLineChart(els.priceChart, state.prices);
  renderDepthChart(els.depthChart, state.bids, state.asks);
  renderVolumeChart(els.volumeChart, state.volumeBuckets);
  renderStrategies(state.strategies || []);
  renderLifecycle(state);
  renderSystems(state.system || {});
  renderCsvFiles(state.csvFiles || [], state.replay?.source);
  renderOptimization(state.optimization || { results: [] });
  renderStrategyCharts(state.strategies || []);

  model.previousSequence = state.sequence;
  model.previousTradeId = state.trades[0] ? tradeKey(state.trades[0]) : null;
}

function renderCsvFiles(files, selectedPath) {
  const current = els.csvSelect.value || selectedPath;
  els.csvSelect.innerHTML = files.map((file) => `
    <option value="${escapeHtml(file.path)}">${escapeHtml(file.fileName)} (${file.rows})</option>
  `).join("");
  if (current && files.some((file) => file.path === current)) {
    els.csvSelect.value = current;
  }
  const selected = files.find((file) => file.path === els.csvSelect.value) || files[0];
  if (!selected) {
    els.csvMetadata.textContent = "No CSV files found in data/";
    return;
  }
  els.csvMetadata.innerHTML = `
    <b>${escapeHtml(selected.fileName)}</b><br>
    ${selected.rows} events · t=${selected.startTimestamp} to ${selected.endTimestamp}<br>
    ${escapeHtml(selected.schema)} · ${selected.columns.map(escapeHtml).join(", ")}
  `;
}

function renderMetrics(state) {
  const metrics = state.metrics;
  const bestBid = state.bids[0]?.price ?? "--";
  const bestAsk = state.asks[0]?.price ?? "--";
  const last = state.trades[0];

  els.ordersProcessed.textContent = metrics.ordersProcessed;
  els.tradesExecuted.textContent = metrics.tradesExecuted;
  els.volumeExecuted.textContent = metrics.volumeExecuted;
  els.spread.textContent = metrics.spread || "--";
  els.latency.textContent = `${metrics.avgLatencyUs.toFixed(1)}µs`;
  els.throughput.textContent = `${metrics.ordersPerSecond.toFixed(1)}/s`;
  els.canceledOrders.textContent = metrics.canceledOrders ?? 0;
  els.modifiedOrders.textContent = metrics.modifiedOrders ?? 0;
  els.pendingEvents.textContent = metrics.pendingEvents ?? 0;
  els.droppedEvents.textContent = metrics.droppedEvents ?? 0;
  els.fillDelay.textContent = `${(metrics.avgFillDelayMs ?? 0).toFixed(1)}ms`;
  els.missedTrades.textContent = metrics.missedTradeOpportunities ?? 0;
  els.totalSlippage.textContent = money(metrics.totalSlippage ?? 0);
  els.bestBid.textContent = bestBid;
  els.bestAsk.textContent = bestAsk;
  els.runState.textContent = state.replayRunning ? "Replaying" : (state.running ? "Running" : "Stopped");
  els.speedInput.value = state.speed;
  els.speedValue.textContent = state.speed;
  els.orderLimitInput.value = state.orderLimit;
  els.replaySpeedInput.value = state.replaySpeed;
  els.replaySpeedValue.textContent = state.replaySpeed;
  els.orderLatencyInput.value = state.orderSubmissionLatencyMs ?? els.orderLatencyInput.value;
  els.marketLatencyInput.value = state.marketDataLatencyMs ?? els.marketLatencyInput.value;
  els.replayStatus.textContent = state.replay?.loaded
    ? `${state.replay.position}/${state.replay.total} events at timestamp ${state.replay.timestamp}`
    : "No CSV loaded";
  els.feeInput.value = state.strategyConfig?.feePerShare ?? els.feeInput.value;
  els.slippageInput.value = state.strategyConfig?.slippagePerShare ?? els.slippageInput.value;
  els.tradePrints.textContent = `${metrics.tradesExecuted} prints`;
  els.lastPrice.textContent = last ? `Last ${last.price} × ${last.quantity}` : "No trades";
}

function renderSystems(system) {
  els.processPhase.textContent = system.processPhase || "running";

  els.threadRoles.innerHTML = (system.threads || []).map((thread) => `
    <div class="system-row">
      <span class="system-name">${escapeHtml(thread.name)}</span>
      <b>${escapeHtml(thread.state)}</b>
      <span>${escapeHtml(thread.primitive)}</span>
    </div>
  `).join("") || `<div class="mini-empty">No thread data yet</div>`;

  els.queueHealth.innerHTML = (system.queues || []).map((queue) => {
    const capacity = Number(queue.capacity || 0);
    const size = Number(queue.size || 0);
    const fill = capacity > 0 ? Math.min(100, (size / capacity) * 100) : 0;
    const tone = Number(queue.dropped || 0) > 0 ? "ask" : "bid";
    return `
      <div class="queue-row">
        <div>
          <span class="system-name">${escapeHtml(queue.name)}</span>
          <b class="${tone}">${size}/${capacity || "∞"}</b>
          <span>${escapeHtml(queue.primitive)}</span>
        </div>
        <i><em style="width:${fill}%"></em></i>
        <small>Dropped ${Number(queue.dropped || 0)}</small>
      </div>
    `;
  }).join("") || `<div class="mini-empty">No queue data yet</div>`;

  els.syncMap.innerHTML = (system.synchronization || []).map((item) => `
    <div class="system-row sync-row">
      <span class="system-name">${escapeHtml(item.state)}</span>
      <b>${escapeHtml(item.guard)}</b>
    </div>
  `).join("") || `<div class="mini-empty">No synchronization data yet</div>`;
}

function renderStrategies(strategies) {
  els.strategyRows.innerHTML = strategies.map((strategy) => {
    const pnlClass = strategy.pnl >= 0 ? "positive" : "negative";
    return `
      <tr>
        <td>${strategy.name}</td>
        <td class="${pnlClass}">${money(strategy.pnl)}</td>
        <td>${strategy.inventory}</td>
        <td>${strategy.trades}</td>
        <td>${percent(strategy.winRate)}</td>
        <td>${strategy.avgFillPrice ? strategy.avgFillPrice.toFixed(2) : "--"}</td>
        <td class="negative">${money(strategy.maxDrawdown)}</td>
        <td>${strategy.sharpeLike.toFixed(2)}</td>
        <td>${strategy.exposureTime}</td>
        <td>${money(strategy.feesPaid + strategy.slippagePaid)}</td>
        <td>${strategy.volume}</td>
        <td>${strategy.activeOrderCount ?? 0}</td>
        <td>${strategy.canceledOrders ?? 0}/${strategy.modifiedOrders ?? 0}</td>
        <td>${strategy.missedTradeOpportunities ?? 0}</td>
        <td>${((strategy.averageFillDelayMs ?? 0)).toFixed(1)}ms</td>
      </tr>
    `;
  }).join("");
}

function renderLifecycle(state) {
  const active = state.activeOrders || [];
  const events = state.lifecycleEvents || [];

  els.activeOrders.innerHTML = active.slice(0, 18).map((order) => `
    <div class="mini-row ${order.side === "Buy" ? "bid-bg" : "ask-bg"}">
      <span class="${order.side === "Buy" ? "bid" : "ask"}">#${order.id}</span>
      <b>${order.side}</b>
      <span>${order.price}</span>
      <span>x${order.quantity}</span>
    </div>
  `).join("") || `<div class="mini-empty">No resting orders</div>`;

  els.lifecycleEvents.innerHTML = events.slice(0, 18).map((event) => {
    const tone = event.type === "canceled" ? "ask" : (event.type === "modified" ? "gold" : "bid");
    return `
      <div class="mini-row">
        <span class="${tone}">${event.type}</span>
        <b>#${event.orderId}</b>
        <span>t=${event.timestamp}</span>
        <span>${event.quantity ? `${event.price} x${event.quantity}` : ""}</span>
      </div>
    `;
  }).join("") || `<div class="mini-empty">No lifecycle events yet</div>`;
}

function renderStrategyCharts(strategies) {
  renderMultiLineChart(
    els.pnlChart,
    strategies.map((strategy) => ({ name: strategy.name, values: strategy.pnlCurve || [] })),
    "Waiting for strategy P&L"
  );
  renderMultiLineChart(
    els.riskChart,
    strategies.flatMap((strategy) => [
      { name: `${strategy.name} DD`, values: strategy.drawdownCurve || [] },
      { name: `${strategy.name} Inv`, values: strategy.inventoryCurve || [] }
    ]),
    "Waiting for risk data"
  );
}

function renderOptimization(optimization) {
  const results = optimization.results || [];
  els.optimizationStatus.textContent = optimization.status || "Idle";
  if (!results.length) {
    els.optimizationRows.innerHTML = "";
    els.bestConfig.textContent = "No optimization run yet";
    renderMultiLineChart(els.optimizationChart, [], "Run optimization to see curves");
    els.parameterHeatmap.innerHTML = `<div class="mini-empty">No parameter scores yet</div>`;
    return;
  }

  const best = results[0];
  els.bestConfig.innerHTML = `
    <b>Best ${escapeHtml(best.strategy)}</b>: ${paramsText(best.parameters)}
    · OOS ${money(best.outOfSample.pnl)}
    · score ${Number(best.rankScore).toFixed(2)}
  `;

  els.optimizationRows.innerHTML = results.map((result, index) => `
    <tr>
      <td>${index + 1}</td>
      <td>${paramsText(result.parameters)}</td>
      <td>${money(result.inSample.pnl)}</td>
      <td class="${result.outOfSample.pnl >= 0 ? "positive" : "negative"}">${money(result.outOfSample.pnl)}</td>
      <td>${result.outOfSample.trades}</td>
      <td>${percent(result.outOfSample.winRate || 0)}</td>
      <td class="negative">${money(result.outOfSample.maxDrawdown || 0)}</td>
      <td>${Number(result.outOfSample.sharpeLike || 0).toFixed(2)}</td>
      <td>${money((result.outOfSample.feesPaid || 0) + (result.outOfSample.slippagePaid || 0))}</td>
      <td>${result.outOfSample.volume}</td>
    </tr>
  `).join("");

  renderMultiLineChart(
    els.optimizationChart,
    [
      { name: "OOS P&L", values: best.outOfSample.pnlCurve || [] },
      { name: "OOS Drawdown", values: best.outOfSample.drawdownCurve || [] },
      { name: "OOS Inventory", values: best.outOfSample.inventoryCurve || [] }
    ],
    "No out-of-sample curve data"
  );

  const scores = results.map((result) => Math.abs(result.rankScore));
  const maxScore = Math.max(...scores, 1);
  els.parameterHeatmap.innerHTML = results.slice(0, 24).map((result) => {
    const intensity = Math.min(0.85, Math.abs(result.rankScore) / maxScore);
    const color = result.rankScore >= 0
      ? `rgba(55,216,147,${0.12 + intensity * 0.55})`
      : `rgba(255,102,115,${0.12 + intensity * 0.55})`;
    return `
      <div class="heat-cell" style="background:${color}">
        <strong>${Number(result.rankScore).toFixed(2)}</strong>
        ${paramsText(result.parameters)}
      </div>
    `;
  }).join("");
}

function renderBook(state) {
  const asks = [...state.asks].reverse();
  const bids = state.bids;
  const maxQty = Math.max(
    1,
    ...state.bids.map((level) => level.quantity),
    ...state.asks.map((level) => level.quantity)
  );

  const askRows = asks.map((level, index) => bookRow(level, "ask", maxQty, index === asks.length - 1));
  const bidRows = bids.map((level, index) => bookRow(level, "bid", maxQty, index === 0));

  els.bookRows.innerHTML = [
    ...askRows,
    `<div class="book-row mid"><span></span><span class="book-price">SPREAD ${state.metrics.spread || "--"}</span><span></span></div>`,
    ...bidRows
  ].join("");
}

function bookRow(level, side, maxQty, best) {
  const width = Math.max(5, Math.min(45, (level.quantity / maxQty) * 45));
  const bestClass = best ? (side === "bid" ? "best-bid" : "best-ask") : "";
  const sideClass = side === "bid" ? "bid" : "ask";
  const barClass = side === "bid" ? "bid-bar" : "ask-bar";
  const bidQty = side === "bid" ? level.quantity : "";
  const askQty = side === "ask" ? level.quantity : "";

  return `
    <div class="book-row ${bestClass}">
      <i class="bar ${barClass}" style="width:${width}%"></i>
      <span class="qty bid">${bidQty}</span>
      <span class="book-price ${sideClass}">${level.price}</span>
      <span class="ask ask-qty">${askQty}</span>
    </div>
  `;
}

function renderTape(state) {
  els.tradeTape.innerHTML = state.trades.slice(0, 22).map((trade, index) => {
    const isNew = index === 0 && tradeKey(trade) !== model.previousTradeId;
    return `
      <div class="trade-row ${isNew ? "new" : ""}">
        <span class="bid">#${trade.buyOrderId}</span>
        <span class="ask">#${trade.sellOrderId}</span>
        <strong>${trade.price}</strong>
        <span>${trade.quantity}</span>
      </div>
    `;
  }).join("");
}

function renderLineChart(canvas, prices) {
  const ctx = setupCanvas(canvas);
  drawGrid(ctx, canvas);

  if (!prices.length) {
    drawEmpty(ctx, canvas, "Waiting for executed trades");
    return;
  }

  const min = Math.min(...prices, 95) - 1;
  const max = Math.max(...prices, 105) + 1;
  const pad = 34;
  const x = (i) => pad + (i / Math.max(1, prices.length - 1)) * (canvas.width - pad * 2);
  const y = (p) => canvas.height - pad - ((p - min) / (max - min)) * (canvas.height - pad * 2);

  const fill = ctx.createLinearGradient(0, pad, 0, canvas.height - pad);
  fill.addColorStop(0, "rgba(244,190,82,0.34)");
  fill.addColorStop(1, "rgba(244,190,82,0.02)");

  ctx.beginPath();
  prices.forEach((price, i) => i === 0 ? ctx.moveTo(x(i), y(price)) : ctx.lineTo(x(i), y(price)));
  ctx.lineTo(x(prices.length - 1), canvas.height - pad);
  ctx.lineTo(x(0), canvas.height - pad);
  ctx.closePath();
  ctx.fillStyle = fill;
  ctx.fill();

  ctx.beginPath();
  prices.forEach((price, i) => i === 0 ? ctx.moveTo(x(i), y(price)) : ctx.lineTo(x(i), y(price)));
  ctx.strokeStyle = "#f4be52";
  ctx.lineWidth = 3;
  ctx.stroke();

  ctx.fillStyle = "#6ec7e0";
  ctx.beginPath();
  ctx.arc(x(prices.length - 1), y(prices[prices.length - 1]), 5, 0, Math.PI * 2);
  ctx.fill();
}

function renderDepthChart(canvas, bids, asks) {
  const ctx = setupCanvas(canvas);
  drawGrid(ctx, canvas);

  const bidDepth = cumulative([...bids].reverse());
  const askDepth = cumulative(asks);
  const all = [...bidDepth, ...askDepth];
  if (!all.length) {
    drawEmpty(ctx, canvas, "No book depth yet");
    return;
  }

  const minPrice = Math.min(...all.map((point) => point.price)) - 1;
  const maxPrice = Math.max(...all.map((point) => point.price)) + 1;
  const maxQty = Math.max(...all.map((point) => point.quantity), 1);
  const pad = 30;
  const x = (price) => pad + ((price - minPrice) / (maxPrice - minPrice)) * (canvas.width - pad * 2);
  const y = (qty) => canvas.height - pad - (qty / maxQty) * (canvas.height - pad * 2);

  drawDepthSide(ctx, bidDepth, x, y, canvas.height - pad, "#37d893", "rgba(55,216,147,0.18)");
  drawDepthSide(ctx, askDepth, x, y, canvas.height - pad, "#ff6673", "rgba(255,102,115,0.18)");
}

function renderVolumeChart(canvas, buckets) {
  const ctx = setupCanvas(canvas);
  drawGrid(ctx, canvas);

  if (!buckets.length) {
    drawEmpty(ctx, canvas, "No executed volume yet");
    return;
  }

  const pad = 28;
  const max = Math.max(...buckets.map((bucket) => bucket.volume), 1);
  const barWidth = Math.max(3, (canvas.width - pad * 2) / buckets.length - 2);

  buckets.forEach((bucket, index) => {
    const height = (bucket.volume / max) * (canvas.height - pad * 2);
    const x = pad + index * ((canvas.width - pad * 2) / buckets.length);
    const y = canvas.height - pad - height;
    ctx.fillStyle = index === buckets.length - 1 ? "#f4be52" : "rgba(110,199,224,0.72)";
    ctx.fillRect(x, y, barWidth, height);
  });
}

function renderMultiLineChart(canvas, series, emptyText) {
  const ctx = setupCanvas(canvas);
  drawGrid(ctx, canvas);

  const active = series.filter((item) => item.values.length);
  if (!active.length) {
    drawEmpty(ctx, canvas, emptyText);
    return;
  }

  const colors = ["#37d893", "#ff6673", "#f4be52", "#6ec7e0", "#b78cff", "#e79052", "#74d7b1", "#ff99a4"];
  const values = active.flatMap((item) => item.values);
  const min = Math.min(...values, 0);
  const max = Math.max(...values, 1);
  const pad = 30;

  active.forEach((item, index) => {
    const line = item.values;
    const x = (i) => pad + (i / Math.max(1, line.length - 1)) * (canvas.width - pad * 2);
    const y = (v) => canvas.height - pad - ((v - min) / Math.max(1, max - min)) * (canvas.height - pad * 2);

    ctx.beginPath();
    line.forEach((value, i) => i === 0 ? ctx.moveTo(x(i), y(value)) : ctx.lineTo(x(i), y(value)));
    ctx.strokeStyle = colors[index % colors.length];
    ctx.lineWidth = 2;
    ctx.stroke();
  });
}

function cumulative(levels) {
  let quantity = 0;
  return levels.map((level) => {
    quantity += level.quantity;
    return { price: level.price, quantity };
  });
}

function drawDepthSide(ctx, points, x, y, bottom, stroke, fill) {
  if (!points.length) return;
  ctx.beginPath();
  points.forEach((point, index) => index === 0 ? ctx.moveTo(x(point.price), y(point.quantity)) : ctx.lineTo(x(point.price), y(point.quantity)));
  ctx.lineTo(x(points[points.length - 1].price), bottom);
  ctx.lineTo(x(points[0].price), bottom);
  ctx.closePath();
  ctx.fillStyle = fill;
  ctx.fill();

  ctx.beginPath();
  points.forEach((point, index) => index === 0 ? ctx.moveTo(x(point.price), y(point.quantity)) : ctx.lineTo(x(point.price), y(point.quantity)));
  ctx.strokeStyle = stroke;
  ctx.lineWidth = 2;
  ctx.stroke();
}

function setupCanvas(canvas) {
  const rect = canvas.getBoundingClientRect();
  canvas.width = Math.max(320, Math.floor(rect.width));
  canvas.height = Math.max(220, Math.floor(rect.height));
  return canvas.getContext("2d");
}

function drawGrid(ctx, canvas) {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = "#101319";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.strokeStyle = "rgba(255,255,255,0.065)";
  ctx.lineWidth = 1;
  for (let i = 1; i < 5; i += 1) {
    const y = (canvas.height / 5) * i;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(canvas.width, y);
    ctx.stroke();
  }
}

function drawEmpty(ctx, canvas, text) {
  ctx.fillStyle = "#9fa7b4";
  ctx.font = "14px ui-sans-serif, system-ui";
  ctx.textAlign = "center";
  ctx.fillText(text, canvas.width / 2, canvas.height / 2);
}

function tradeKey(trade) {
  return `${trade.buyOrderId}-${trade.sellOrderId}-${trade.timestamp}-${trade.quantity}`;
}

function money(value) {
  return `${value < 0 ? "-" : ""}$${Math.abs(value).toFixed(2)}`;
}

function percent(value) {
  return `${(value * 100).toFixed(1)}%`;
}

function paramsText(params) {
  return Object.entries(params || {})
    .map(([key, value]) => `${key.replaceAll("_", " ")}=${Number(value).toFixed(value % 1 ? 2 : 0)}`)
    .join(", ");
}

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

els.startBtn.addEventListener("click", () => sendControl(true));
els.stopBtn.addEventListener("click", () => sendControl(false));
els.replayPlayBtn.addEventListener("click", () => sendReplayControl(!(model.state?.replayRunning)));
els.replayStepBtn.addEventListener("click", stepReplay);
els.loadReplayBtn.addEventListener("click", loadReplay);
els.uploadCsvBtn.addEventListener("click", uploadCsv);
els.csvSelect.addEventListener("change", loadReplay);
els.resetBtn.addEventListener("click", resetBackend);
els.runOptimizationBtn.addEventListener("click", runOptimization);
els.exportOptimizationBtn.addEventListener("click", () => {
  window.location.href = "/api/optimization/export";
});
els.jumpBtn.addEventListener("click", jumpReplay);
els.speedInput.addEventListener("input", () => {
  els.speedValue.textContent = els.speedInput.value;
  if (model.state) sendControl(model.state.running);
});
els.replaySpeedInput.addEventListener("input", () => {
  els.replaySpeedValue.textContent = els.replaySpeedInput.value;
  if (model.state) sendReplayControl(model.state.replayRunning);
});
els.feeInput.addEventListener("change", () => model.state && sendControl(model.state.running));
els.slippageInput.addEventListener("change", () => model.state && sendControl(model.state.running));
els.orderLatencyInput.addEventListener("change", () => model.state && sendControl(model.state.running));
els.marketLatencyInput.addEventListener("change", () => model.state && sendControl(model.state.running));
els.orderLimitInput.addEventListener("change", () => {
  if (model.state) sendControl(model.state.running);
});
els.orderForm.addEventListener("submit", sendManualOrder);
els.orderType.addEventListener("change", () => {
  els.priceInput.disabled = els.orderType.value === "Market";
});
window.addEventListener("resize", () => {
  if (model.state) render();
});

connect();
refreshCsvFiles();
