export default class Mover {
    speed = 2;
    label = "hi";
    started = false;
    Start() {
        this.started = true;
        console.log("start " + this.gameObject.name);
    }
    Update(dt) {
        this.transform.position = this.transform.position.add(new Vector3(this.speed * dt, 0, 0));
    }
}
