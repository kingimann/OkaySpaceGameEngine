export default class P {
    moved = 0;
    dtSeen = 0;
    Update(dt) {
        this.dtSeen = Time.deltaTime;
        if (Input.GetKey("d")) {
            this.moved++;
            this.transform.Translate(Vector3.right.scale(dt));
        }
    }
}
