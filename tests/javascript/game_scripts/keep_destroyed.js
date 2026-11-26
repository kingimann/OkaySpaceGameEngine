export default {
    Start() { this.keep = GameObject.Find("Victim"); },
    Update(dt) { if (this.keep) { const n = this.keep.name; } },
};
