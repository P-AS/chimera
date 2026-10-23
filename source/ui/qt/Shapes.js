// What Lua's gui and forms libraries draw, painted on a Canvas: one shape,
// in the canvas's own units after whatever scale its caller set. sx and sy
// are that scale (a line stays one pixel wide), urlOf names an image file.
.pragma library

function draw(ctx, canvas, s, sx, sy, urlOf) {
    ctx.lineWidth = 1 / Math.min(sx, sy)
    if (s.k === "rect") {
        if (s.fill !== "") { ctx.fillStyle = s.fill; ctx.fillRect(s.x, s.y, s.w, s.h) }
        if (s.line !== "") { ctx.strokeStyle = s.line; ctx.strokeRect(s.x + 0.5 / sx, s.y + 0.5 / sy, s.w, s.h) }
    } else if (s.k === "ellipse") {
        ctx.beginPath()
        ctx.ellipse(s.x, s.y, s.w, s.h)
        if (s.fill !== "") { ctx.fillStyle = s.fill; ctx.fill() }
        if (s.line !== "") { ctx.strokeStyle = s.line; ctx.stroke() }
    } else if (s.k === "line") {
        ctx.beginPath()
        ctx.moveTo(s.x + 0.5, s.y + 0.5)
        ctx.lineTo(s.x2 + 0.5, s.y2 + 0.5)
        ctx.strokeStyle = s.line
        ctx.stroke()
    } else if (s.k === "path" || s.k === "bezier") {
        if (s.points.length === 0) return
        ctx.beginPath()
        ctx.moveTo(s.points[0][0] + 0.5, s.points[0][1] + 0.5)
        if (s.k === "bezier")
            ctx.bezierCurveTo(s.points[1][0] + 0.5, s.points[1][1] + 0.5, s.points[2][0] + 0.5, s.points[2][1] + 0.5, s.points[3][0] + 0.5, s.points[3][1] + 0.5)
        else
            for (let i = 1; i < s.points.length; i++) ctx.lineTo(s.points[i][0] + 0.5, s.points[i][1] + 0.5)
        if (s.closed) ctx.closePath()
        if (s.fill !== undefined && s.fill !== "") { ctx.fillStyle = s.fill; ctx.fill() }
        if (s.line !== "") { ctx.strokeStyle = s.line; ctx.stroke() }
    } else if (s.k === "pie") {
        // an ellipse's slice (or its arc alone): drawn on a circle, stretched to the ellipse
        ctx.beginPath()
        ctx.translate(s.x + s.w / 2, s.y + s.h / 2)
        ctx.scale(s.w / 2 || 1, s.h / 2 || 1)
        if (!s.arc) ctx.moveTo(0, 0)
        ctx.arc(0, 0, 1, s.start * Math.PI / 180, (s.start + s.sweep) * Math.PI / 180, s.sweep < 0)
        if (!s.arc) ctx.closePath()
        ctx.lineWidth = 2 / Math.min(sx * s.w, sy * s.h)
        if (s.fill !== "") { ctx.fillStyle = s.fill; ctx.fill() }
        if (s.line !== "") { ctx.strokeStyle = s.line; ctx.stroke() }
    } else if (s.k === "image") {
        const url = urlOf(s.file, s.epoch)
        if (!canvas.isImageLoaded(url)) { canvas.loadImage(url); return }
        if (s.sw !== undefined) ctx.drawImage(url, s.sx, s.sy, s.sw, s.sh, s.x, s.y, s.w, s.h)
        else if (s.w >= 0 && s.h >= 0) ctx.drawImage(url, s.x, s.y, s.w, s.h)
        else ctx.drawImage(url, s.x, s.y)
    } else if (s.k === "text") {
        ctx.font = s.size + "px monospace"
        ctx.textBaseline = "top"
        if (s.back !== "") { ctx.fillStyle = s.back; ctx.fillRect(s.x, s.y, ctx.measureText(s.text).width, s.size) }
        ctx.fillStyle = s.color
        ctx.fillText(s.text, s.x, s.y)
    }
}

// a file as an address the canvas loads; a new epoch reads it again (clearImageCache)
function urlOf(file, epoch) {
    const path = file.replace(/\\/g, "/")
    return (path.startsWith("/") ? "file://" : "file:///") + encodeURI(path).replace(/#/g, "%23").replace(/\?/g, "%3F") + (epoch > 0 ? "?" + epoch : "")
}
