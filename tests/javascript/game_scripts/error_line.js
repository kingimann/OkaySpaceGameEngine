export default class E {
    n = 0;
    Update(dt) {
        this.n++;
        if (this.n === 2) {
            const missing = undefined;
            missing.field = 1;
        }
    }
}
